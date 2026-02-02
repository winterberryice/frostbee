/*
 * TEST 04: Raw I2C Communication at 400kHz (Fast Mode)
 *
 * PURPOSE: Same as test 03, but reconfigures I2C to 400kHz at runtime.
 *          If test 03 works but this fails, your wiring or pull-ups
 *          cannot handle fast-mode speeds.
 *
 * WHAT TO CHECK:
 *   - Compare results with test 03
 *   - If this fails but 03 works = pull-ups too weak or wires too long
 *   - If both fail = not a speed issue
 *
 * EXPECTED SERIAL OUTPUT (success):
 *   [TEST 04] Reconfigured to 400kHz
 *   [TEST 04] Serial number: 0xXXXXXXXX
 *   [TEST 04] >>> SUCCESS at 400kHz! <<<
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(test_04, LOG_LEVEL_INF);

#define I2C_NODE    DT_NODELABEL(i2c0)
#define SHT40_ADDR  0x44

#define SHT40_CMD_READ_SERIAL    0x89
#define SHT40_CMD_MEASURE_HIGH   0xFD

#define LED0_NODE DT_ALIAS(led0)
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

static int sht40_read_serial(const struct device *i2c_dev)
{
	uint8_t cmd = SHT40_CMD_READ_SERIAL;
	uint8_t buf[6];
	int ret;

	LOG_INF("Sending read-serial command (0x%02X)...", cmd);

	ret = i2c_write(i2c_dev, &cmd, 1, SHT40_ADDR);
	if (ret != 0) {
		LOG_ERR("i2c_write failed: %d", ret);
		return ret;
	}

	k_msleep(1);

	ret = i2c_read(i2c_dev, buf, sizeof(buf), SHT40_ADDR);
	if (ret != 0) {
		LOG_ERR("i2c_read failed: %d", ret);
		return ret;
	}

	LOG_INF("Raw bytes: %02X %02X %02X %02X %02X %02X",
		buf[0], buf[1], buf[2], buf[3], buf[4], buf[5]);

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

	ret = i2c_write(i2c_dev, &cmd, 1, SHT40_ADDR);
	if (ret != 0) {
		LOG_ERR("i2c_write (measure) failed: %d", ret);
		return ret;
	}

	k_msleep(10);

	ret = i2c_read(i2c_dev, buf, sizeof(buf), SHT40_ADDR);
	if (ret != 0) {
		LOG_ERR("i2c_read (measure) failed: %d", ret);
		return ret;
	}

	uint16_t raw_temp = ((uint16_t)buf[0] << 8) | buf[1];
	uint16_t raw_hum  = ((uint16_t)buf[3] << 8) | buf[4];

	int temp_milli = -45000 + (175000 * (int32_t)raw_temp) / 65535;
	int hum_milli  = -6000  + (125000 * (int32_t)raw_hum)  / 65535;

	if (hum_milli < 0) hum_milli = 0;
	if (hum_milli > 100000) hum_milli = 100000;

	LOG_INF("Temp: %d.%02d C   Hum: %d.%02d %%RH",
		temp_milli / 1000, (temp_milli % 1000) / 10,
		hum_milli / 1000, (hum_milli % 1000) / 10);

	return 0;
}

int main(void)
{
	const struct device *i2c_dev = DEVICE_DT_GET(I2C_NODE);
	int ret;

	LOG_INF("========================================");
	LOG_INF("TEST 04: Raw I2C at 400kHz (Fast Mode)");
	LOG_INF("SDA=P0.20  SCL=P0.22  ADDR=0x%02X", SHT40_ADDR);
	LOG_INF("========================================");

	if (!device_is_ready(i2c_dev)) {
		LOG_ERR("I2C device not ready!");
		return -1;
	}

	/* Reconfigure bus speed to 400kHz at runtime */
	ret = i2c_configure(i2c_dev, I2C_SPEED_SET(I2C_SPEED_FAST) | I2C_MODE_CONTROLLER);
	if (ret != 0) {
		LOG_ERR("Failed to set 400kHz: %d", ret);
		LOG_ERR("Falling back to default speed (100kHz from overlay).");
	} else {
		LOG_INF("Reconfigured I2C to 400kHz (fast mode)");
	}

	if (gpio_is_ready_dt(&led)) {
		gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
	}

	/* Try read serial at 400kHz */
	ret = sht40_read_serial(i2c_dev);
	if (ret == 0) {
		LOG_INF(">>> SUCCESS at 400kHz! <<<");
	} else {
		LOG_ERR(">>> FAILED at 400kHz <<<");
		LOG_ERR("If test 03 (100kHz) worked, your pull-ups or");
		LOG_ERR("wiring cannot handle fast mode. Use 100kHz.");
	}

	LOG_INF("");
	LOG_INF("Measuring every 3 seconds...");

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
