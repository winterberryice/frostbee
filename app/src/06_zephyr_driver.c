/*
 * TEST 06: Zephyr SHT4X Sensor Driver
 *
 * PURPOSE: Use Zephyr's built-in sht4x sensor driver instead of raw I2C.
 *          This is the "proper" way to use the sensor in production.
 *          The driver is configured via devicetree in the overlay file.
 *
 * WHAT TO CHECK:
 *   - If this works = use this approach going forward
 *   - If this fails but test 03 works = driver config issue
 *   - Check that CONFIG_SENSOR=y is in prj.conf
 *
 * EXPECTED SERIAL OUTPUT (success):
 *   [TEST 06] SHT4X sensor device ready
 *   [TEST 06] Temperature: 23.45 C
 *   [TEST 06] Humidity:    48.12 %RH
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(test_06, LOG_LEVEL_INF);

#define LED0_NODE DT_ALIAS(led0)
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

int main(void)
{
	const struct device *sht = DEVICE_DT_GET_ANY(sensirion_sht4x);

	/* Wait for USB serial to connect */
	for (int i = 10; i > 0; i--) {
		printk("Starting in %d...\n", i);
		k_msleep(1000);
	}

	LOG_INF("========================================");
	LOG_INF("TEST 06: Zephyr SHT4X Sensor Driver");
	LOG_INF("Using built-in driver via devicetree");
	LOG_INF("========================================");

	if (sht == NULL) {
		LOG_ERR("No SHT4X device found in devicetree!");
		LOG_ERR("Check that overlay has sht4x@44 node under &i2c0");
		return -1;
	}

	if (!device_is_ready(sht)) {
		LOG_ERR("SHT4X device exists but is not ready!");
		LOG_ERR("Possible causes:");
		LOG_ERR("  - I2C bus not initialized");
		LOG_ERR("  - Sensor not responding at startup");
		LOG_ERR("  - Check wiring and pull-ups");
		return -1;
	}

	LOG_INF("SHT4X sensor device is ready!");

	if (gpio_is_ready_dt(&led)) {
		gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
	}

	LOG_INF("Reading sensor every 3 seconds...");
	LOG_INF("");

	int reading = 0;

	while (1) {
		struct sensor_value temp, hum;
		int ret;

		reading++;

		ret = sensor_sample_fetch(sht);
		if (ret != 0) {
			LOG_ERR("[reading %d] sensor_sample_fetch failed: %d", reading, ret);
			k_msleep(3000);
			continue;
		}

		ret = sensor_channel_get(sht, SENSOR_CHAN_AMBIENT_TEMP, &temp);
		if (ret != 0) {
			LOG_ERR("[reading %d] get temperature failed: %d", reading, ret);
		}

		ret = sensor_channel_get(sht, SENSOR_CHAN_HUMIDITY, &hum);
		if (ret != 0) {
			LOG_ERR("[reading %d] get humidity failed: %d", reading, ret);
		}

		LOG_INF("[reading %d] Temp: %d.%06d C  Hum: %d.%06d %%RH",
			reading,
			temp.val1, temp.val2,
			hum.val1, hum.val2);

		if (gpio_is_ready_dt(&led)) {
			gpio_pin_toggle_dt(&led);
		}

		k_msleep(3000);
	}

	return 0;
}
