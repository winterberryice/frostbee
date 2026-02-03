/*
 * TEST 05: Soft Reset Then Read
 *
 * PURPOSE: The SHT40 might be stuck from a previously interrupted I2C
 *          transaction. This test sends a soft-reset command first,
 *          waits for the sensor to recover, then attempts communication.
 *
 * ALSO TRIES: Clock stretching recovery by toggling SCL manually before
 *             initializing I2C, in case the sensor is holding SDA low.
 *
 * WHAT TO CHECK:
 *   - If this works but test 03 fails = sensor was in stuck state
 *   - Power-cycle the sensor (unplug VDD, wait 5s, replug) and retry 03
 *
 * EXPECTED SERIAL OUTPUT (success):
 *   [TEST 05] Sending soft reset (0x94)...
 *   [TEST 05] Waiting 10ms for reset...
 *   [TEST 05] Reading serial number...
 *   [TEST 05] Serial: 0xXXXXXXXX
 *   [TEST 05] >>> SUCCESS after reset! <<<
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(test_05, LOG_LEVEL_INF);

#define I2C_NODE    DT_NODELABEL(i2c0)
#define SHT40_ADDR  0x44

#define SHT40_CMD_SOFT_RESET     0x94
#define SHT40_CMD_READ_SERIAL    0x89
#define SHT40_CMD_MEASURE_HIGH   0xFD

#define LED0_NODE DT_ALIAS(led0)
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

static int sht40_soft_reset(const struct device *i2c_dev)
{
	uint8_t cmd = SHT40_CMD_SOFT_RESET;
	int ret;

	LOG_INF("Sending soft reset command (0x%02X)...", cmd);

	ret = i2c_write(i2c_dev, &cmd, 1, SHT40_ADDR);
	if (ret != 0) {
		LOG_WRN("Soft reset write returned: %d (may be OK if sensor was stuck)", ret);
		/* Even if this fails, wait and try to read anyway */
	} else {
		LOG_INF("Soft reset command sent OK");
	}

	LOG_INF("Waiting 10ms for sensor to reset...");
	k_msleep(10);

	return 0;
}

static int sht40_read_serial(const struct device *i2c_dev)
{
	uint8_t cmd = SHT40_CMD_READ_SERIAL;
	uint8_t buf[6];
	int ret;

	LOG_INF("Sending read-serial command...");

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

	uint32_t serial = ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) |
			  ((uint32_t)buf[3] << 8) | (uint32_t)buf[4];

	LOG_INF("Raw bytes: %02X %02X %02X %02X %02X %02X",
		buf[0], buf[1], buf[2], buf[3], buf[4], buf[5]);
	LOG_INF("Serial: 0x%08X", serial);

	return 0;
}

static int sht40_measure(const struct device *i2c_dev)
{
	uint8_t cmd = SHT40_CMD_MEASURE_HIGH;
	uint8_t buf[6];
	int ret;

	ret = i2c_write(i2c_dev, &cmd, 1, SHT40_ADDR);
	if (ret != 0) {
		LOG_ERR("Measure write failed: %d", ret);
		return ret;
	}

	k_msleep(10);

	ret = i2c_read(i2c_dev, buf, sizeof(buf), SHT40_ADDR);
	if (ret != 0) {
		LOG_ERR("Measure read failed: %d", ret);
		return ret;
	}

	uint16_t raw_temp = ((uint16_t)buf[0] << 8) | buf[1];
	uint16_t raw_hum  = ((uint16_t)buf[3] << 8) | buf[4];

	int temp_milli = -45000 + (int)((175000LL * (int64_t)raw_temp) / 65535);
	int hum_milli  = -6000  + (int)((125000LL * (int64_t)raw_hum)  / 65535);

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

	/* Wait for USB serial to connect */
	for (int i = 10; i > 0; i--) {
		printk("Starting in %d...\n", i);
		k_msleep(1000);
	}

	LOG_INF("========================================");
	LOG_INF("TEST 05: Soft Reset Then Read");
	LOG_INF("SDA=P0.24  SCL=P1.00  ADDR=0x%02X", SHT40_ADDR);
	LOG_INF("========================================");

	if (!device_is_ready(i2c_dev)) {
		LOG_ERR("I2C device not ready!");
		return -1;
	}

	if (gpio_is_ready_dt(&led)) {
		gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
	}

	/* Step 1: Soft reset */
	sht40_soft_reset(i2c_dev);

	/* Step 2: Try multiple resets if first read fails */
	for (int attempt = 1; attempt <= 3; attempt++) {
		LOG_INF("");
		LOG_INF("--- Attempt %d/3 ---", attempt);

		ret = sht40_read_serial(i2c_dev);
		if (ret == 0) {
			LOG_INF(">>> SUCCESS after reset! <<<");
			break;
		}

		LOG_WRN("Attempt %d failed. Resetting again...", attempt);
		sht40_soft_reset(i2c_dev);
		k_msleep(50);  /* Extra wait between retries */
	}

	if (ret != 0) {
		LOG_ERR(">>> FAILED after 3 reset attempts <<<");
		LOG_ERR("Try power-cycling the sensor:");
		LOG_ERR("  1. Disconnect SHT40 VDD");
		LOG_ERR("  2. Wait 5 seconds");
		LOG_ERR("  3. Reconnect VDD");
		LOG_ERR("  4. Reset the nRF");
	}

	LOG_INF("");
	LOG_INF("Measuring every 3 seconds (with reset before each)...");

	while (1) {
		k_msleep(3000);

		if (gpio_is_ready_dt(&led)) {
			gpio_pin_toggle_dt(&led);
		}

		/* Reset before each measurement to be safe */
		sht40_soft_reset(i2c_dev);
		sht40_measure(i2c_dev);
		LOG_INF("---");
	}

	return 0;
}
