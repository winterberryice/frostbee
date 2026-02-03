/*
 * Frostbee - SHT40 Temperature & Humidity Sensor
 *
 * nRF52840 Dongle + Sensirion SHT40 via I2C
 * Pins: SDA = P0.24, SCL = P1.00
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(frostbee, LOG_LEVEL_INF);

#define SENSOR_READ_INTERVAL_MS 3000

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

int main(void)
{
	const struct device *sht = DEVICE_DT_GET_ANY(sensirion_sht4x);

	if (sht == NULL) {
		LOG_ERR("SHT4X device not found in devicetree");
		return -ENODEV;
	}

	if (!device_is_ready(sht)) {
		LOG_ERR("SHT4X device not ready - check wiring and pull-ups");
		return -ENODEV;
	}

	if (gpio_is_ready_dt(&led)) {
		gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
	}

	LOG_INF("Frostbee started - SHT40 sensor ready");

	while (1) {
		struct sensor_value temp, hum;
		int ret;

		ret = sensor_sample_fetch(sht);
		if (ret) {
			LOG_ERR("Sensor fetch failed: %d", ret);
			k_msleep(SENSOR_READ_INTERVAL_MS);
			continue;
		}

		sensor_channel_get(sht, SENSOR_CHAN_AMBIENT_TEMP, &temp);
		sensor_channel_get(sht, SENSOR_CHAN_HUMIDITY, &hum);

		LOG_INF("T: %d.%02d C  H: %d.%02d %%RH",
			temp.val1, temp.val2 / 10000,
			hum.val1, hum.val2 / 10000);

		if (gpio_is_ready_dt(&led)) {
			gpio_pin_toggle_dt(&led);
		}

		k_msleep(SENSOR_READ_INTERVAL_MS);
	}

	return 0;
}
