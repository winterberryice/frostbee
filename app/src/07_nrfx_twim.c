/*
 * TEST 07: NRFX TWIM Direct (Bypass Zephyr I2C Driver)
 *
 * PURPOSE: Use Nordic's NRFX TWIM HAL directly, completely bypassing
 *          Zephyr's I2C driver stack. This isolates whether the issue
 *          is in Zephyr's driver or in the hardware/wiring.
 *
 * IMPORTANT: This test disables Zephyr's I2C driver for i2c0 and
 *            talks to the TWIM peripheral directly.
 *
 * WHAT TO CHECK:
 *   - If this works but tests 03-06 fail = Zephyr driver issue
 *   - If this also fails = hardware/wiring problem
 *
 * EXPECTED SERIAL OUTPUT (success):
 *   [TEST 07] NRFX TWIM initialized on P0.20/P0.22
 *   [TEST 07] TX complete
 *   [TEST 07] RX complete
 *   [TEST 07] Serial: 0xXXXXXXXX
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <nrfx_twim.h>
#include <hal/nrf_gpio.h>

LOG_MODULE_REGISTER(test_07, LOG_LEVEL_INF);

#define SHT40_ADDR  0x44
#define SDA_PIN     NRF_GPIO_PIN_MAP(0, 24)   /* P0.24 */
#define SCL_PIN     NRF_GPIO_PIN_MAP(1, 0)    /* P1.00 */

#define SHT40_CMD_READ_SERIAL   0x89
#define SHT40_CMD_MEASURE_HIGH  0xFD

#define LED0_NODE DT_ALIAS(led0)
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

/* Use TWIM0 instance */
static const nrfx_twim_t twim = NRFX_TWIM_INSTANCE(0);

static volatile bool twim_xfer_done;
static volatile nrfx_err_t twim_xfer_result;

static void twim_handler(nrfx_twim_evt_t const *p_event, void *p_context)
{
	ARG_UNUSED(p_context);

	switch (p_event->type) {
	case NRFX_TWIM_EVT_DONE:
		twim_xfer_done = true;
		twim_xfer_result = NRFX_SUCCESS;
		break;
	case NRFX_TWIM_EVT_ADDRESS_NACK:
		twim_xfer_done = true;
		twim_xfer_result = NRFX_ERROR_DRV_TWI_ERR_ANACK;
		LOG_ERR("NACK on address!");
		break;
	case NRFX_TWIM_EVT_DATA_NACK:
		twim_xfer_done = true;
		twim_xfer_result = NRFX_ERROR_DRV_TWI_ERR_DNACK;
		LOG_ERR("NACK on data!");
		break;
	default:
		twim_xfer_done = true;
		twim_xfer_result = NRFX_ERROR_INTERNAL;
		LOG_ERR("TWIM error event: %d", p_event->type);
		break;
	}
}

static nrfx_err_t twim_write(uint8_t *data, size_t len, uint8_t addr, bool no_stop)
{
	nrfx_twim_xfer_desc_t xfer = {
		.type = no_stop ? NRFX_TWIM_XFER_TX : NRFX_TWIM_XFER_TX,
		.address = addr,
		.primary_length = len,
		.p_primary_buf = data,
	};

	uint32_t flags = no_stop ? NRFX_TWIM_FLAG_TX_NO_STOP : 0;

	twim_xfer_done = false;
	nrfx_err_t err = nrfx_twim_xfer(&twim, &xfer, flags);
	if (err != NRFX_SUCCESS) {
		return err;
	}

	/* Wait for completion */
	int timeout = 1000;
	while (!twim_xfer_done && timeout > 0) {
		k_usleep(100);
		timeout--;
	}

	if (!twim_xfer_done) {
		LOG_ERR("TWIM TX timeout!");
		return NRFX_ERROR_TIMEOUT;
	}

	return twim_xfer_result;
}

static nrfx_err_t twim_read(uint8_t *data, size_t len, uint8_t addr)
{
	nrfx_twim_xfer_desc_t xfer = {
		.type = NRFX_TWIM_XFER_RX,
		.address = addr,
		.primary_length = len,
		.p_primary_buf = data,
	};

	twim_xfer_done = false;
	nrfx_err_t err = nrfx_twim_xfer(&twim, &xfer, 0);
	if (err != NRFX_SUCCESS) {
		return err;
	}

	int timeout = 1000;
	while (!twim_xfer_done && timeout > 0) {
		k_usleep(100);
		timeout--;
	}

	if (!twim_xfer_done) {
		LOG_ERR("TWIM RX timeout!");
		return NRFX_ERROR_TIMEOUT;
	}

	return twim_xfer_result;
}

int main(void)
{
	nrfx_err_t err;

	/* Wait for USB serial to connect */
	for (int i = 10; i > 0; i--) {
		printk("Starting in %d...\n", i);
		k_msleep(1000);
	}

	LOG_INF("========================================");
	LOG_INF("TEST 07: NRFX TWIM Direct");
	LOG_INF("Bypassing Zephyr I2C driver");
	LOG_INF("SDA=P0.24  SCL=P1.00  ADDR=0x%02X", SHT40_ADDR);
	LOG_INF("========================================");

	if (gpio_is_ready_dt(&led)) {
		gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
	}

	/* Configure TWIM */
	nrfx_twim_config_t config = {
		.scl = SCL_PIN,
		.sda = SDA_PIN,
		.frequency = NRF_TWIM_FREQ_100K,
		.interrupt_priority = NRFX_TWIM_DEFAULT_CONFIG_IRQ_PRIORITY,
		.hold_bus_uninit = false,
	};

	/* Connect IRQ */
	IRQ_CONNECT(NRFX_IRQ_NUMBER_GET(NRF_TWIM0),
		    NRFX_TWIM_DEFAULT_CONFIG_IRQ_PRIORITY,
		    nrfx_twim_0_irq_handler, NULL, 0);

	err = nrfx_twim_init(&twim, &config, twim_handler, NULL);
	if (err != NRFX_SUCCESS) {
		LOG_ERR("TWIM init failed: 0x%08X", err);
		LOG_ERR("This might happen if Zephyr already claimed TWIM0.");
		LOG_ERR("Check that i2c0 status is 'disabled' for this test,");
		LOG_ERR("or use TWIM1 instead.");
		return -1;
	}

	nrfx_twim_enable(&twim);
	LOG_INF("NRFX TWIM0 initialized and enabled");

	/* Read serial number */
	LOG_INF("Sending read-serial command...");

	/* The command byte must be in RAM for DMA (nrfx requirement) */
	static uint8_t cmd_buf[1];
	static uint8_t rx_buf[6];

	cmd_buf[0] = SHT40_CMD_READ_SERIAL;
	err = twim_write(cmd_buf, 1, SHT40_ADDR, false);
	if (err != NRFX_SUCCESS) {
		LOG_ERR("TX failed: 0x%08X", err);
		LOG_ERR("Sensor did not ACK. Check wiring.");
	} else {
		LOG_INF("TX complete");

		k_msleep(1);

		err = twim_read(rx_buf, 6, SHT40_ADDR);
		if (err != NRFX_SUCCESS) {
			LOG_ERR("RX failed: 0x%08X", err);
		} else {
			LOG_INF("RX complete");
			LOG_INF("Raw: %02X %02X %02X %02X %02X %02X",
				rx_buf[0], rx_buf[1], rx_buf[2],
				rx_buf[3], rx_buf[4], rx_buf[5]);

			uint32_t serial = ((uint32_t)rx_buf[0] << 24) |
					  ((uint32_t)rx_buf[1] << 16) |
					  ((uint32_t)rx_buf[3] << 8) |
					  (uint32_t)rx_buf[4];
			LOG_INF("Serial: 0x%08X", serial);
			LOG_INF(">>> SUCCESS with NRFX TWIM! <<<");
		}
	}

	/* Continuous measurement */
	LOG_INF("");
	LOG_INF("Measuring every 3 seconds...");

	while (1) {
		k_msleep(3000);

		if (gpio_is_ready_dt(&led)) {
			gpio_pin_toggle_dt(&led);
		}

		cmd_buf[0] = SHT40_CMD_MEASURE_HIGH;
		err = twim_write(cmd_buf, 1, SHT40_ADDR, false);
		if (err != NRFX_SUCCESS) {
			LOG_ERR("Measure TX failed: 0x%08X", err);
			continue;
		}

		k_msleep(10);

		err = twim_read(rx_buf, 6, SHT40_ADDR);
		if (err != NRFX_SUCCESS) {
			LOG_ERR("Measure RX failed: 0x%08X", err);
			continue;
		}

		uint16_t raw_temp = ((uint16_t)rx_buf[0] << 8) | rx_buf[1];
		uint16_t raw_hum  = ((uint16_t)rx_buf[3] << 8) | rx_buf[4];

		int temp_milli = -45000 + (175000 * (int32_t)raw_temp) / 65535;
		int hum_milli  = -6000  + (125000 * (int32_t)raw_hum)  / 65535;

		if (hum_milli < 0) hum_milli = 0;
		if (hum_milli > 100000) hum_milli = 100000;

		LOG_INF("Temp: %d.%02d C   Hum: %d.%02d %%RH",
			temp_milli / 1000, (temp_milli % 1000) / 10,
			hum_milli / 1000, (hum_milli % 1000) / 10);
	}

	return 0;
}
