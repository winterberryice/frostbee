/*
 * TEST 08: Alternate Pins (P0.26 SDA, P0.27 SCL)
 *
 * PURPOSE: Same I2C communication as test 03, but using different pins
 *          via i2c1 (TWIM1). This rules out pin-specific hardware issues
 *          on P0.20/P0.22.
 *
 * WIRING FOR THIS TEST:
 *   - Move SDA wire from P0.20 to P0.26
 *   - Move SCL wire from P0.22 to P0.27
 *   - Keep VDD and GND connected
 *   - Keep pull-ups on the new SDA/SCL lines
 *
 * WHAT TO CHECK:
 *   - If this works but test 03 fails = P0.20/P0.22 pins are damaged
 *     or have conflicting peripherals
 *   - If both fail = not a pin issue
 *
 * EXPECTED SERIAL OUTPUT (success):
 *   [TEST 08] Using i2c1: SDA=P0.26  SCL=P0.27
 *   [TEST 08] Serial: 0xXXXXXXXX
 *   [TEST 08] >>> SUCCESS on alternate pins! <<<
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(test_08, LOG_LEVEL_INF);

/* Using i2c1 instead of i2c0 */
#define I2C_NODE    DT_NODELABEL(i2c1)
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

	/* Wait for USB serial to connect */
	for (int i = 10; i > 0; i--) {
		printk("Starting in %d...\n", i);
		k_msleep(1000);
	}

	LOG_INF("========================================");
	LOG_INF("TEST 08: Alternate Pins");
	LOG_INF("Using i2c1: SDA=P0.26  SCL=P0.27");
	LOG_INF("ADDR=0x%02X", SHT40_ADDR);
	LOG_INF("========================================");
	LOG_INF("");
	LOG_INF("!! REWIRE BEFORE THIS TEST !!");
	LOG_INF("  SDA -> P0.26  (was P0.20)");
	LOG_INF("  SCL -> P0.27  (was P0.22)");
	LOG_INF("  + pull-ups on the new lines");
	LOG_INF("");

	if (!device_is_ready(i2c_dev)) {
		LOG_ERR("I2C1 device not ready!");
		return -1;
	}

	LOG_INF("I2C1 bus ready.");

	if (gpio_is_ready_dt(&led)) {
		gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
	}

	int ret = sht40_read_serial(i2c_dev);
	if (ret == 0) {
		LOG_INF(">>> SUCCESS on alternate pins! <<<");
		LOG_INF("If test 03 failed, your P0.20/P0.22 pins may be damaged");
		LOG_INF("or have conflicting peripheral assignments.");
	} else {
		LOG_ERR(">>> FAILED on alternate pins too <<<");
		LOG_ERR("Not a pin-specific issue. Check pull-ups and power.");
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
