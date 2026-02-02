/*
 * TEST 01: GPIO Pin Wiggle
 *
 * PURPOSE: Verify that your SDA (P0.20) and SCL (P0.22) wires are
 *          actually connected between the nRF and the SHT40 breakout.
 *
 * NO I2C is used here — we just toggle the pins as plain GPIO.
 *
 * WHAT TO CHECK:
 *   - Use a multimeter on the SDA/SCL lines
 *   - You should see voltage toggling every 1 second
 *   - If you don't see toggling, check your wiring
 *   - The on-board LED also blinks so you know the firmware is running
 *
 * EXPECTED SERIAL OUTPUT:
 *   [TEST 01] GPIO Pin Wiggle — verify SDA/SCL wiring
 *   [TEST 01] SDA=P0.20  SCL=P0.22
 *   [TEST 01] Toggling pins... (check with multimeter)
 *   [TEST 01] SDA=HIGH  SCL=HIGH
 *   [TEST 01] SDA=LOW   SCL=LOW
 *   ...repeats...
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(test_01, LOG_LEVEL_INF);

/* SDA and SCL pins — matching the overlay */
#define SDA_PORT_NODE DT_NODELABEL(gpio0)
#define SDA_PIN       20
#define SCL_PIN       22

/* LED for visual heartbeat */
#define LED0_NODE DT_ALIAS(led0)
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

int main(void)
{
	const struct device *gpio0 = DEVICE_DT_GET(SDA_PORT_NODE);

	/* Wait for USB serial to connect */
	for (int i = 10; i > 0; i--) {
		printk("Starting in %d...\n", i);
		k_msleep(1000);
	}

	LOG_INF("========================================");
	LOG_INF("TEST 01: GPIO Pin Wiggle");
	LOG_INF("Verify SDA/SCL wiring with multimeter");
	LOG_INF("========================================");
	LOG_INF("SDA = P0.%d", SDA_PIN);
	LOG_INF("SCL = P0.%d", SCL_PIN);

	if (!device_is_ready(gpio0)) {
		LOG_ERR("GPIO0 port not ready!");
		return -1;
	}

	/* Configure SDA and SCL as plain outputs */
	gpio_pin_configure(gpio0, SDA_PIN, GPIO_OUTPUT_LOW);
	gpio_pin_configure(gpio0, SCL_PIN, GPIO_OUTPUT_LOW);

	/* Configure LED */
	if (gpio_is_ready_dt(&led)) {
		gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
	}

	LOG_INF("Toggling pins every 1 second...");
	LOG_INF("Check SDA/SCL with multimeter or logic analyzer");
	LOG_INF("");

	bool state = false;
	int cycle = 0;

	while (1) {
		state = !state;
		cycle++;

		gpio_pin_set(gpio0, SDA_PIN, (int)state);
		gpio_pin_set(gpio0, SCL_PIN, (int)state);

		if (gpio_is_ready_dt(&led)) {
			gpio_pin_toggle_dt(&led);
		}

		LOG_INF("[cycle %d] SDA=%s  SCL=%s",
			cycle,
			state ? "HIGH" : "LOW",
			state ? "HIGH" : "LOW");

		k_msleep(1000);
	}

	return 0;
}
