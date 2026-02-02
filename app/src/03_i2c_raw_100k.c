/*
 * TEST 03: Raw I2C Communication at 100kHz
 *
 * PURPOSE: Send the SHT40 "read serial number" command (0x89) using
 *          Zephyr's I2C API at standard speed (100kHz).
 *
 * SHT40 PROTOCOL:
 *   1. Write command byte 0x89 to address 0x44
 *   2. Wait 1ms
 *   3. Read 6 bytes back (serial number + CRC)
 *
 * WHAT TO CHECK:
 *   - "Serial number: XXXX" = success, I2C works!
 *   - "i2c_write failed" = sensor not responding to writes
 *   - "i2c_read failed" = sensor not responding to reads
 *
 * EXPECTED SERIAL OUTPUT (success):
 *   [TEST 03] Sending read-serial command (0x89)...
 *   [TEST 03] Raw bytes: XX XX XX XX XX XX
 *   [TEST 03] Serial number: 0xXXXXXXXX
 *   [TEST 03] >>> SUCCESS: I2C communication works! <<<
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(test_03, LOG_LEVEL_INF);

#define I2C_NODE    DT_NODELABEL(i2c0)
#define SHT40_ADDR  0x44

/* SHT40 commands */
#define SHT40_CMD_READ_SERIAL    0x89
#define SHT40_CMD_MEASURE_HIGH   0xFD

/* LED for visual heartbeat */
#define LED0_NODE DT_ALIAS(led0)
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

/* Simple CRC-8 check for SHT40 (polynomial 0x31, init 0xFF) */
static uint8_t sht40_crc8(const uint8_t *data, size_t len)
{
	uint8_t crc = 0xFF;

	for (size_t i = 0; i < len; i++) {
		crc ^= data[i];
		for (int bit = 0; bit < 8; bit++) {
			if (crc & 0x80) {
				crc = (crc << 1) ^ 0x31;
			} else {
				crc <<= 1;
			}
		}
	}
	return crc;
}

static int sht40_read_serial(const struct device *i2c_dev)
{
	uint8_t cmd = SHT40_CMD_READ_SERIAL;
	uint8_t buf[6];
	int ret;

	LOG_INF("Sending read-serial command (0x%02X) to 0x%02X...",
		cmd, SHT40_ADDR);

	/* Step 1: Write command */
	ret = i2c_write(i2c_dev, &cmd, 1, SHT40_ADDR);
	if (ret != 0) {
		LOG_ERR("i2c_write failed: %d", ret);
		LOG_ERR("Sensor did not ACK the command byte.");
		return ret;
	}

	LOG_INF("Write OK. Waiting 1ms...");
	k_msleep(1);

	/* Step 2: Read 6 bytes */
	ret = i2c_read(i2c_dev, buf, sizeof(buf), SHT40_ADDR);
	if (ret != 0) {
		LOG_ERR("i2c_read failed: %d", ret);
		LOG_ERR("Sensor did not respond to read request.");
		return ret;
	}

	LOG_INF("Raw bytes: %02X %02X %02X %02X %02X %02X",
		buf[0], buf[1], buf[2], buf[3], buf[4], buf[5]);

	/* Check CRC */
	uint8_t crc1 = sht40_crc8(&buf[0], 2);
	uint8_t crc2 = sht40_crc8(&buf[3], 2);

	if (crc1 != buf[2]) {
		LOG_WRN("CRC mismatch on first word: got 0x%02X, expected 0x%02X",
			buf[2], crc1);
	}
	if (crc2 != buf[5]) {
		LOG_WRN("CRC mismatch on second word: got 0x%02X, expected 0x%02X",
			buf[5], crc2);
	}

	uint32_t serial = ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) |
			  ((uint32_t)buf[3] << 8) | (uint32_t)buf[4];

	LOG_INF("Serial number: 0x%08X", serial);

	return 0;
}

static int sht40_measure(const struct device *i2c_dev)
{
	uint8_t cmd = SHT40_CMD_MEASURE_HIGH;
	uint8_t buf[6];
	int ret;

	LOG_INF("Sending measure command (0x%02X)...", cmd);

	ret = i2c_write(i2c_dev, &cmd, 1, SHT40_ADDR);
	if (ret != 0) {
		LOG_ERR("i2c_write (measure) failed: %d", ret);
		return ret;
	}

	/* High repeatability measurement takes up to 10ms */
	k_msleep(10);

	ret = i2c_read(i2c_dev, buf, sizeof(buf), SHT40_ADDR);
	if (ret != 0) {
		LOG_ERR("i2c_read (measure) failed: %d", ret);
		return ret;
	}

	LOG_INF("Raw bytes: %02X %02X %02X %02X %02X %02X",
		buf[0], buf[1], buf[2], buf[3], buf[4], buf[5]);

	/* Convert raw to temperature and humidity */
	uint16_t raw_temp = ((uint16_t)buf[0] << 8) | buf[1];
	uint16_t raw_hum  = ((uint16_t)buf[3] << 8) | buf[4];

	/* SHT40 formula: T = -45 + 175 * raw / 65535 */
	int temp_milli = -45000 + (175000 * (int32_t)raw_temp) / 65535;
	int hum_milli  = -6000  + (125000 * (int32_t)raw_hum)  / 65535;

	if (hum_milli < 0) hum_milli = 0;
	if (hum_milli > 100000) hum_milli = 100000;

	LOG_INF("Temperature: %d.%02d C", temp_milli / 1000,
		(temp_milli % 1000) / 10);
	LOG_INF("Humidity:    %d.%02d %%RH", hum_milli / 1000,
		(hum_milli % 1000) / 10);

	return 0;
}

int main(void)
{
	const struct device *i2c_dev = DEVICE_DT_GET(I2C_NODE);

	LOG_INF("========================================");
	LOG_INF("TEST 03: Raw I2C at 100kHz");
	LOG_INF("SDA=P0.20  SCL=P0.22  ADDR=0x%02X", SHT40_ADDR);
	LOG_INF("========================================");

	if (!device_is_ready(i2c_dev)) {
		LOG_ERR("I2C device not ready!");
		return -1;
	}

	LOG_INF("I2C bus ready.");

	/* Configure LED */
	if (gpio_is_ready_dt(&led)) {
		gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
	}

	/* Read serial number first */
	int ret = sht40_read_serial(i2c_dev);
	if (ret == 0) {
		LOG_INF(">>> SUCCESS: I2C communication works! <<<");
	} else {
		LOG_ERR(">>> FAILED: Could not read serial number <<<");
	}

	LOG_INF("");
	LOG_INF("Now reading temperature/humidity every 3 seconds...");

	/* Continuous measurement loop */
	while (1) {
		k_msleep(3000);

		if (gpio_is_ready_dt(&led)) {
			gpio_pin_toggle_dt(&led);
		}

		sht40_measure(i2c_dev);
		LOG_INF("---");
	}

	return 0;
}
