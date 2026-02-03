/*
 * TEST 09: Verbose Debug with Retries
 *
 * PURPOSE: Maximum logging and multiple retry strategies to capture
 *          the exact failure mode. Use this to gather debug info
 *          before asking for help.
 *
 * THIS TEST TRIES:
 *   1. Check I2C bus configuration
 *   2. Try both addresses (0x44 and 0x45)
 *   3. Try write-then-read vs separate write/read
 *   4. Try with delays between operations
 *   5. Retry each operation 5 times
 *   6. Report detailed error codes
 *
 * WHAT TO CHECK:
 *   - Copy ALL the serial output — it contains diagnostic info
 *   - Look for which step first fails and what error code
 *
 * EXPECTED SERIAL OUTPUT:
 *   Lots of detailed debug output for every step
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(test_09, LOG_LEVEL_DBG);

#define I2C_NODE DT_NODELABEL(i2c0)

#define SHT40_ADDR_A  0x44
#define SHT40_ADDR_B  0x45

#define SHT40_CMD_READ_SERIAL    0x89
#define SHT40_CMD_SOFT_RESET     0x94
#define SHT40_CMD_MEASURE_HIGH   0xFD
#define SHT40_CMD_MEASURE_LOW    0xE0

#define LED0_NODE DT_ALIAS(led0)
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

static const char *i2c_err_str(int err)
{
	switch (err) {
	case 0:       return "OK";
	case -EIO:    return "EIO (I/O error - NACK or bus error)";
	case -EBUSY:  return "EBUSY (bus busy)";
	case -EAGAIN: return "EAGAIN (try again)";
	case -ENXIO:  return "ENXIO (no such device)";
	case -ETIMEDOUT: return "ETIMEDOUT (timeout)";
	default:      return "UNKNOWN";
	}
}

static void test_single_byte_write(const struct device *i2c_dev, uint8_t addr,
				    uint8_t cmd, const char *desc)
{
	LOG_INF("  [%s] Writing 0x%02X to addr 0x%02X...", desc, cmd, addr);

	for (int attempt = 1; attempt <= 5; attempt++) {
		int ret = i2c_write(i2c_dev, &cmd, 1, addr);
		LOG_INF("    attempt %d: ret=%d (%s)", attempt, ret, i2c_err_str(ret));

		if (ret == 0) {
			LOG_INF("    >>> WRITE SUCCESS <<<");
			return;
		}
		k_msleep(10);
	}
	LOG_ERR("    >>> ALL 5 WRITE ATTEMPTS FAILED <<<");
}

static void test_read_after_cmd(const struct device *i2c_dev, uint8_t addr,
				uint8_t cmd, int delay_ms, const char *desc)
{
	uint8_t buf[6] = {0};
	int ret;

	LOG_INF("  [%s] Write 0x%02X, wait %dms, read 6 bytes from 0x%02X",
		desc, cmd, delay_ms, addr);

	/* Write */
	ret = i2c_write(i2c_dev, &cmd, 1, addr);
	LOG_INF("    write: ret=%d (%s)", ret, i2c_err_str(ret));
	if (ret != 0) {
		LOG_ERR("    Write failed, skipping read.");
		return;
	}

	k_msleep(delay_ms);

	/* Read */
	ret = i2c_read(i2c_dev, buf, 6, addr);
	LOG_INF("    read:  ret=%d (%s)", ret, i2c_err_str(ret));
	if (ret != 0) {
		LOG_ERR("    Read failed.");
		return;
	}

	LOG_INF("    data: %02X %02X %02X %02X %02X %02X",
		buf[0], buf[1], buf[2], buf[3], buf[4], buf[5]);

	/* If it looks like a serial number response */
	if (cmd == SHT40_CMD_READ_SERIAL) {
		uint32_t serial = ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) |
				  ((uint32_t)buf[3] << 8) | (uint32_t)buf[4];
		LOG_INF("    serial: 0x%08X", serial);
	}

	/* If it looks like a measurement response */
	if (cmd == SHT40_CMD_MEASURE_HIGH || cmd == SHT40_CMD_MEASURE_LOW) {
		uint16_t raw_temp = ((uint16_t)buf[0] << 8) | buf[1];
		uint16_t raw_hum  = ((uint16_t)buf[3] << 8) | buf[4];

		int temp_milli = -45000 + (int)((175000LL * (int64_t)raw_temp) / 65535);
		int hum_milli  = -6000  + (int)((125000LL * (int64_t)raw_hum)  / 65535);

		if (hum_milli < 0) hum_milli = 0;
		if (hum_milli > 100000) hum_milli = 100000;

		LOG_INF("    temp: %d.%02d C   hum: %d.%02d %%RH",
			temp_milli / 1000, (temp_milli % 1000) / 10,
			hum_milli / 1000, (hum_milli % 1000) / 10);
	}

	LOG_INF("    >>> READ SUCCESS <<<");
}

static void test_write_read_combined(const struct device *i2c_dev, uint8_t addr,
				      uint8_t cmd, const char *desc)
{
	uint8_t buf[6] = {0};
	int ret;

	LOG_INF("  [%s] i2c_write_read 0x%02X to addr 0x%02X, read 6 bytes",
		desc, cmd, addr);

	ret = i2c_write_read(i2c_dev, addr, &cmd, 1, buf, 6);
	LOG_INF("    ret=%d (%s)", ret, i2c_err_str(ret));

	if (ret == 0) {
		LOG_INF("    data: %02X %02X %02X %02X %02X %02X",
			buf[0], buf[1], buf[2], buf[3], buf[4], buf[5]);
		LOG_INF("    >>> COMBINED WRITE/READ SUCCESS <<<");
	} else {
		LOG_ERR("    >>> COMBINED WRITE/READ FAILED <<<");
	}
}

static void scan_quick(const struct device *i2c_dev)
{
	LOG_INF("  Quick scan around SHT40 addresses:");

	for (uint8_t addr = 0x40; addr <= 0x50; addr++) {
		struct i2c_msg msg;
		uint8_t dummy;

		msg.buf = &dummy;
		msg.len = 0;
		msg.flags = I2C_MSG_WRITE | I2C_MSG_STOP;

		int ret = i2c_transfer(i2c_dev, &msg, 1, addr);
		if (ret == 0) {
			LOG_INF("    0x%02X: FOUND!", addr);
		}
	}
}

int main(void)
{
	const struct device *i2c_dev = DEVICE_DT_GET(I2C_NODE);

	/* Wait for USB serial to connect */
	for (int i = 10; i > 0; i--) {
		printk("Starting in %d...\n", i);
		k_msleep(1000);
	}

	LOG_INF("================================================");
	LOG_INF("TEST 09: Verbose Debug with Retries");
	LOG_INF("SDA=P0.24  SCL=P1.00");
	LOG_INF("This test tries EVERYTHING. Copy all output.");
	LOG_INF("================================================");
	LOG_INF("");

	if (!device_is_ready(i2c_dev)) {
		LOG_ERR("!!! I2C device not ready !!!");
		LOG_ERR("This means Zephyr could not initialize the I2C peripheral.");
		LOG_ERR("Check:");
		LOG_ERR("  - CONFIG_I2C=y in prj.conf");
		LOG_ERR("  - Overlay has i2c0 with status='okay'");
		LOG_ERR("  - pinctrl is configured");
		return -1;
	}

	LOG_INF("I2C device ready: OK");

	if (gpio_is_ready_dt(&led)) {
		gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
	}

	/* ── PHASE 1: Quick scan ── */
	LOG_INF("");
	LOG_INF("=== PHASE 1: Quick bus scan ===");
	scan_quick(i2c_dev);

	/* ── PHASE 2: Try address 0x44 ── */
	LOG_INF("");
	LOG_INF("=== PHASE 2: Address 0x44 (SHT40-AD1B) ===");

	LOG_INF("Step 2a: Soft reset");
	test_single_byte_write(i2c_dev, SHT40_ADDR_A, SHT40_CMD_SOFT_RESET, "reset-44");
	k_msleep(10);

	LOG_INF("Step 2b: Read serial (separate write/read, 1ms delay)");
	test_read_after_cmd(i2c_dev, SHT40_ADDR_A, SHT40_CMD_READ_SERIAL, 1, "serial-44-1ms");

	LOG_INF("Step 2c: Read serial (separate write/read, 10ms delay)");
	test_read_after_cmd(i2c_dev, SHT40_ADDR_A, SHT40_CMD_READ_SERIAL, 10, "serial-44-10ms");

	LOG_INF("Step 2d: Read serial (combined write_read)");
	test_write_read_combined(i2c_dev, SHT40_ADDR_A, SHT40_CMD_READ_SERIAL, "serial-44-combined");

	LOG_INF("Step 2e: Measure high precision");
	test_read_after_cmd(i2c_dev, SHT40_ADDR_A, SHT40_CMD_MEASURE_HIGH, 10, "meas-44-high");

	LOG_INF("Step 2f: Measure low precision");
	test_read_after_cmd(i2c_dev, SHT40_ADDR_A, SHT40_CMD_MEASURE_LOW, 2, "meas-44-low");

	/* ── PHASE 3: Try address 0x45 ── */
	LOG_INF("");
	LOG_INF("=== PHASE 3: Address 0x45 (SHT40-BD1B) ===");

	LOG_INF("Step 3a: Soft reset");
	test_single_byte_write(i2c_dev, SHT40_ADDR_B, SHT40_CMD_SOFT_RESET, "reset-45");
	k_msleep(10);

	LOG_INF("Step 3b: Read serial");
	test_read_after_cmd(i2c_dev, SHT40_ADDR_B, SHT40_CMD_READ_SERIAL, 1, "serial-45");

	LOG_INF("Step 3c: Measure");
	test_read_after_cmd(i2c_dev, SHT40_ADDR_B, SHT40_CMD_MEASURE_HIGH, 10, "meas-45");

	/* ── PHASE 4: Retry with longer delays ── */
	LOG_INF("");
	LOG_INF("=== PHASE 4: Extended delays ===");

	LOG_INF("Step 4a: Reset, wait 100ms, then read serial");
	test_single_byte_write(i2c_dev, SHT40_ADDR_A, SHT40_CMD_SOFT_RESET, "reset-long");
	k_msleep(100);
	test_read_after_cmd(i2c_dev, SHT40_ADDR_A, SHT40_CMD_READ_SERIAL, 50, "serial-long-delay");

	LOG_INF("Step 4b: Measure with 50ms wait");
	test_read_after_cmd(i2c_dev, SHT40_ADDR_A, SHT40_CMD_MEASURE_HIGH, 50, "meas-long-delay");

	/* ── DONE ── */
	LOG_INF("");
	LOG_INF("================================================");
	LOG_INF("TEST 09 COMPLETE");
	LOG_INF("Copy ALL output above and analyze results.");
	LOG_INF("Look for which phase/step first shows SUCCESS.");
	LOG_INF("================================================");

	/* Keep blinking LED so you know it's alive */
	while (1) {
		k_msleep(1000);
		if (gpio_is_ready_dt(&led)) {
			gpio_pin_toggle_dt(&led);
		}
	}

	return 0;
}
