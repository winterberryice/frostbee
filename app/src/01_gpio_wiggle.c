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
#include <hal/nrf_gpio.h>
#include <hal/nrf_twim.h>

LOG_MODULE_REGISTER(test_01, LOG_LEVEL_INF);

/*
 * ┌─────────────────────────────────────────┐
 * │  CHANGE THESE TO TEST DIFFERENT PINS:   │
 * │                                         │
 * │  P0.xx → NRF_GPIO_PIN_MAP(0, xx)       │
 * │  P1.xx → NRF_GPIO_PIN_MAP(1, xx)       │
 * └─────────────────────────────────────────┘
 */
#define SDA_ABS  NRF_GPIO_PIN_MAP(0, 24)   /* P0.24 */
#define SCL_ABS  NRF_GPIO_PIN_MAP(1, 0)    /* P1.00 */

/* LED for visual heartbeat */
#define LED0_NODE DT_ALIAS(led0)
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

int main(void)
{
	/* Wait for USB serial to connect */
	for (int i = 10; i > 0; i--) {
		printk("Starting in %d...\n", i);
		k_msleep(1000);
	}

	LOG_INF("========================================");
	LOG_INF("TEST 01: GPIO Pin Wiggle");
	LOG_INF("Verify SDA/SCL wiring with multimeter");
	LOG_INF("========================================");
	LOG_INF("SDA = P%d.%02d (abs %d)", SDA_ABS / 32, SDA_ABS % 32, SDA_ABS);
	LOG_INF("SCL = P%d.%02d (abs %d)", SCL_ABS / 32, SCL_ABS % 32, SCL_ABS);

	/*
	 * Disable ALL I2C peripherals and disconnect their pins.
	 * Both TWIM0 and TWIM1 are enabled in the overlay, so
	 * Zephyr claims pins at boot. We must fully release them.
	 */
	LOG_INF("Disabling TWIM0 and TWIM1...");

	NRF_TWIM0->ENABLE = 0;
	NRF_TWIM0->PSEL.SDA = 0xFFFFFFFF;
	NRF_TWIM0->PSEL.SCL = 0xFFFFFFFF;

	NRF_TWIM1->ENABLE = 0;
	NRF_TWIM1->PSEL.SDA = 0xFFFFFFFF;
	NRF_TWIM1->PSEL.SCL = 0xFFFFFFFF;

	/*
	 * Configure pins using direct nRF register access.
	 * nrf_gpio_* functions handle both P0 and P1 ports.
	 */
	LOG_INF("Configuring pins as GPIO outputs...");

	nrf_gpio_cfg_output(SDA_ABS);
	nrf_gpio_cfg_output(SCL_ABS);

	/* Start both LOW */
	nrf_gpio_pin_clear(SDA_ABS);
	nrf_gpio_pin_clear(SCL_ABS);

	/* Read back to verify */
	uint32_t sda_in = nrf_gpio_pin_read(SDA_ABS);
	uint32_t scl_in = nrf_gpio_pin_read(SCL_ABS);
	uint32_t sda_out = nrf_gpio_pin_out_read(SDA_ABS);
	uint32_t scl_out = nrf_gpio_pin_out_read(SCL_ABS);

	LOG_INF("After clear — OUT reg: SDA=%d SCL=%d", sda_out, scl_out);
	LOG_INF("After clear — IN  reg: SDA=%d SCL=%d", sda_in, scl_in);

	if (sda_in != 0 || scl_in != 0) {
		LOG_WRN("Pin reads HIGH despite being set LOW!");
		LOG_WRN("Something external may be pulling the line up");
		LOG_WRN("(pull-up resistor? sensor? short to VCC?)");
	}

	/* Configure LED */
	if (gpio_is_ready_dt(&led)) {
		gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
	}

	LOG_INF("");
	LOG_INF("Toggling pins every 1 second...");
	LOG_INF("Measure with multimeter: should alternate 0V / 3.3V");
	LOG_INF("");

	bool state = false;
	int cycle = 0;

	while (1) {
		state = !state;
		cycle++;

		/* Toggle using nrf_gpio (handles both P0 and P1) */
		if (state) {
			nrf_gpio_pin_set(SDA_ABS);
			nrf_gpio_pin_set(SCL_ABS);
		} else {
			nrf_gpio_pin_clear(SDA_ABS);
			nrf_gpio_pin_clear(SCL_ABS);
		}

		/* Read back what the pin actually sees */
		sda_in = nrf_gpio_pin_read(SDA_ABS);
		scl_in = nrf_gpio_pin_read(SCL_ABS);

		if (gpio_is_ready_dt(&led)) {
			gpio_pin_toggle_dt(&led);
		}

		LOG_INF("[cycle %d] SET=%s  READ: SDA=%s SCL=%s",
			cycle,
			state ? "HIGH" : "LOW",
			sda_in ? "HIGH" : "LOW",
			scl_in ? "HIGH" : "LOW");

		k_msleep(1000);
	}

	return 0;
}
