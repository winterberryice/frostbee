/*
 * TEST 02: I2C Bus Scan
 *
 * PURPOSE: Scan all 127 possible I2C addresses to see which devices
 *          respond with ACK. The SHT40 should appear at 0x44 or 0x45.
 *
 * WHAT TO CHECK:
 *   - If you see "Found device at 0x44" — sensor is electrically OK
 *   - If you see "Found device at 0x45" — sensor is the B variant
 *   - If NO devices found — wiring or pull-up issue
 *
 * EXPECTED SERIAL OUTPUT (success):
 *   [TEST 02] I2C Bus Scan
 *   [TEST 02] Scanning 127 addresses on i2c0...
 *   [TEST 02] 0x44: *** FOUND DEVICE ***
 *   [TEST 02] Scan complete: 1 device(s) found
 *
 * EXPECTED SERIAL OUTPUT (failure):
 *   [TEST 02] Scan complete: 0 device(s) found
 *   [TEST 02] >>> No devices! Check wiring and pull-ups <<<
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(test_02, LOG_LEVEL_INF);

#define I2C_NODE DT_NODELABEL(i2c0)

/* LED for visual heartbeat */
#define LED0_NODE DT_ALIAS(led0)
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

int main(void)
{
	const struct device *i2c_dev = DEVICE_DT_GET(I2C_NODE);
	int found = 0;

	LOG_INF("========================================");
	LOG_INF("TEST 02: I2C Bus Scan");
	LOG_INF("Looking for devices on i2c0 (100kHz)");
	LOG_INF("SDA=P0.20  SCL=P0.22");
	LOG_INF("========================================");

	if (!device_is_ready(i2c_dev)) {
		LOG_ERR("I2C device not ready! Check overlay/config.");
		return -1;
	}

	LOG_INF("I2C bus is ready. Scanning 127 addresses...");
	LOG_INF("");

	/* Configure LED */
	if (gpio_is_ready_dt(&led)) {
		gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
	}

	/*
	 * Scan every valid 7-bit address (0x03 to 0x77).
	 * Addresses 0x00-0x02 and 0x78-0x7F are reserved.
	 * We do a zero-length write — if the device ACKs, it exists.
	 */
	for (uint8_t addr = 0x03; addr <= 0x77; addr++) {
		struct i2c_msg msg;
		uint8_t dummy;

		msg.buf = &dummy;
		msg.len = 0;
		msg.flags = I2C_MSG_WRITE | I2C_MSG_STOP;

		int ret = i2c_transfer(i2c_dev, &msg, 1, addr);

		if (ret == 0) {
			LOG_INF("  0x%02X: *** FOUND DEVICE ***", addr);
			found++;

			if (addr == 0x44) {
				LOG_INF("         ^ This is SHT40-AD1B (most common)");
			} else if (addr == 0x45) {
				LOG_INF("         ^ This is SHT40-BD1B (B variant)");
			}
		}
	}

	LOG_INF("");
	LOG_INF("Scan complete: %d device(s) found", found);

	if (found == 0) {
		LOG_WRN(">>> No devices found! <<<");
		LOG_WRN("Check:");
		LOG_WRN("  1. Wiring: SDA to P0.20, SCL to P0.22");
		LOG_WRN("  2. Pull-ups: 4.7k or 10k to VDD on SDA & SCL");
		LOG_WRN("  3. Power: SHT40 VDD connected to 3.3V");
		LOG_WRN("  4. Ground: SHT40 GND connected to nRF GND");
	}

	LOG_INF("");
	LOG_INF("Repeating scan every 5 seconds...");

	/* Repeat scan in a loop so you can fiddle with wires */
	while (1) {
		k_msleep(5000);

		if (gpio_is_ready_dt(&led)) {
			gpio_pin_toggle_dt(&led);
		}

		found = 0;
		for (uint8_t addr = 0x03; addr <= 0x77; addr++) {
			struct i2c_msg msg;
			uint8_t dummy;

			msg.buf = &dummy;
			msg.len = 0;
			msg.flags = I2C_MSG_WRITE | I2C_MSG_STOP;

			int ret = i2c_transfer(i2c_dev, &msg, 1, addr);
			if (ret == 0) {
				LOG_INF("  0x%02X: FOUND", addr);
				found++;
			}
		}
		LOG_INF("  -> %d device(s) found", found);
	}

	return 0;
}
