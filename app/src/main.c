/*
 * nRF52840 Dongle - All Pin Test
 *
 * Configures available GPIO pins as inputs with internal pull-ups.
 * Touch a pin to GND to simulate a button press.
 * The triggered pin is logged via console and the on-board LED toggles
 * as visual feedback.
 *
 * Excluded pins:
 *   P0.00, P0.01 - 32.768 kHz crystal (LFXO)
 *   P0.15        - On-board LED (used for visual feedback)
 *   P0.18        - nRESET
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

/* On-board LED for visual feedback */
#define LED_NODE DT_ALIAS(led0)
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED_NODE, gpios);

/* Debounce time - ignore repeated triggers within this window */
#define DEBOUNCE_MS 300

/*
 * Pins to test on each port.
 * Adjust these arrays to match the pins physically accessible on your dongle.
 */
static const uint8_t p0_test_pins[] = {
	2, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
	16, 17, 19, 20, 21, 22, 23, 24, 25, 26, 29, 31
};

static const uint8_t p1_test_pins[] = {
	0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15
};

static const struct device *gpio0_dev;
static const struct device *gpio1_dev;

static struct gpio_callback gpio0_cb_data;
static struct gpio_callback gpio1_cb_data;

static volatile int64_t last_press_time;

static void port0_handler(const struct device *dev, struct gpio_callback *cb,
			  uint32_t pins)
{
	int64_t now = k_uptime_get();

	if ((now - last_press_time) < DEBOUNCE_MS) {
		return;
	}
	last_press_time = now;

	for (int i = 0; i < 32; i++) {
		if (pins & BIT(i)) {
			printk("[PIN HIT] P0.%02d  (port=0, pin=%d, time=%lld ms)\n",
			       i, i, now);
			gpio_pin_toggle_dt(&led);
		}
	}
}

static void port1_handler(const struct device *dev, struct gpio_callback *cb,
			  uint32_t pins)
{
	int64_t now = k_uptime_get();

	if ((now - last_press_time) < DEBOUNCE_MS) {
		return;
	}
	last_press_time = now;

	for (int i = 0; i < 16; i++) {
		if (pins & BIT(i)) {
			printk("[PIN HIT] P1.%02d  (port=1, pin=%d, time=%lld ms)\n",
			       i, i, now);
			gpio_pin_toggle_dt(&led);
		}
	}
}

static int configure_test_pins(const struct device *port, const char *port_name,
			       const uint8_t *pins, size_t count,
			       uint32_t *mask_out)
{
	uint32_t mask = 0;
	int configured = 0;

	for (size_t i = 0; i < count; i++) {
		uint8_t pin = pins[i];
		int ret;

		ret = gpio_pin_configure(port, pin,
					 GPIO_INPUT | GPIO_PULL_UP);
		if (ret < 0) {
			printk("  WARN: %s.%02d configure failed (err %d)\n",
			       port_name, pin, ret);
			continue;
		}

		ret = gpio_pin_interrupt_configure(port, pin,
						   GPIO_INT_EDGE_FALLING);
		if (ret < 0) {
			printk("  WARN: %s.%02d interrupt failed (err %d)\n",
			       port_name, pin, ret);
			continue;
		}

		mask |= BIT(pin);
		configured++;
	}

	*mask_out = mask;
	return configured;
}

int main(void)
{
	uint32_t p0_mask = 0;
	uint32_t p1_mask = 0;
	int count;

	printk("\n");
	printk("=========================================\n");
	printk("  nRF52840 Dongle - All Pin Test\n");
	printk("=========================================\n");
	printk("Touch any configured pin to GND.\n");
	printk("LED toggles + console log on each hit.\n\n");

	/* Setup LED */
	if (!gpio_is_ready_dt(&led)) {
		printk("ERROR: LED device not ready\n");
		return 0;
	}
	gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);

	/* Get GPIO port devices */
	gpio0_dev = DEVICE_DT_GET(DT_NODELABEL(gpio0));
	gpio1_dev = DEVICE_DT_GET(DT_NODELABEL(gpio1));

	if (!device_is_ready(gpio0_dev)) {
		printk("ERROR: GPIO port 0 not ready\n");
		return 0;
	}
	if (!device_is_ready(gpio1_dev)) {
		printk("ERROR: GPIO port 1 not ready\n");
		return 0;
	}

	/* Configure Port 0 test pins */
	printk("Configuring Port 0 pins...\n");
	count = configure_test_pins(gpio0_dev, "P0", p0_test_pins,
				    ARRAY_SIZE(p0_test_pins), &p0_mask);
	printk("  Port 0: %d/%d pins ready (mask=0x%08x)\n",
	       count, (int)ARRAY_SIZE(p0_test_pins), p0_mask);

	/* Configure Port 1 test pins */
	printk("Configuring Port 1 pins...\n");
	count = configure_test_pins(gpio1_dev, "P1", p1_test_pins,
				    ARRAY_SIZE(p1_test_pins), &p1_mask);
	printk("  Port 1: %d/%d pins ready (mask=0x%08x)\n",
	       count, (int)ARRAY_SIZE(p1_test_pins), p1_mask);

	/* Register interrupt callbacks */
	gpio_init_callback(&gpio0_cb_data, port0_handler, p0_mask);
	gpio_add_callback(gpio0_dev, &gpio0_cb_data);

	gpio_init_callback(&gpio1_cb_data, port1_handler, p1_mask);
	gpio_add_callback(gpio1_dev, &gpio1_cb_data);

	printk("\n>> Ready! Touch pins to GND to test. <<\n\n");

	/* Keep alive - the work happens in interrupt callbacks */
	while (1) {
		k_sleep(K_FOREVER);
	}

	return 0;
}
