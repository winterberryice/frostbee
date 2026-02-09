/*
 * Frostbee - Zigbee Temperature & Humidity Sensor
 *
 * nRF52840 Dongle + Sensirion SHT40 via I2C
 * Zigbee Sleepy End Device with ZCL clusters:
 *   - Basic, Identify, Power Configuration
 *   - Temperature Measurement, Relative Humidity
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/reboot.h>
#include <ram_pwrdn.h>

#include <zboss_api.h>
#include <zboss_api_addons.h>
#include <zigbee/zigbee_app_utils.h>
#include <zigbee/zigbee_error_handler.h>
#include <zb_nrf_platform.h>
#include "zb_mem_config_custom.h"
#include "zb_frostbee.h"

LOG_MODULE_REGISTER(frostbee, LOG_LEVEL_INF);

/* Forward declaration for button handler */
static void sensor_read_and_update(zb_bufid_t bufid);

/* Sensor read interval in seconds (used for ZBOSS alarm scheduling). */
#define SENSOR_READ_INTERVAL_S  10  /* 10s for dev, 600s for production */

/* Reset button timing (milliseconds) */
#define BUTTON_DEBOUNCE_MS         100    /* Ignore edges within this window */
#define BUTTON_SHORT_PRESS_MAX_MS  1000   /* < 1s = short press (restart) */
#define BUTTON_FACTORY_RESET_MS    5000   /* >= 5s = factory reset */

/* Reset button GPIO */
#define RESET_BUTTON_NODE DT_ALIAS(sw0)

/* Keep Zigbee network data across reboots.
 * Device remembers paired network - no rejoin needed after restart.
 * To force rejoin: erase NVRAM via nrfjprog or flash with ZB_TRUE temporarily.
 */
#define ERASE_PERSISTENT_CONFIG ZB_FALSE

/* Basic cluster metadata */
#define FROSTBEE_INIT_BASIC_APP_VERSION    1
#define FROSTBEE_INIT_BASIC_STACK_VERSION  10
#define FROSTBEE_INIT_BASIC_HW_VERSION     1
#define FROSTBEE_INIT_BASIC_MANUF_NAME     "Frostbee"
#define FROSTBEE_INIT_BASIC_MODEL_ID       "FBE_TH_1"
#define FROSTBEE_INIT_BASIC_DATE_CODE      "20250201"
#define FROSTBEE_INIT_BASIC_LOCATION_DESC  ""
#define FROSTBEE_INIT_BASIC_PH_ENV         ZB_ZCL_BASIC_ENV_UNSPECIFIED

/* Temperature measurement range: -40.00 C to +125.00 C (SHT40 spec) */
#define FROSTBEE_TEMP_MIN_VALUE  (-4000)
#define FROSTBEE_TEMP_MAX_VALUE  12500

/* Humidity measurement range: 0.00% to 100.00% */
#define FROSTBEE_HUM_MIN_VALUE   0
#define FROSTBEE_HUM_MAX_VALUE   10000

/* ─── Device context (ZCL attribute storage) ─── */

struct zb_device_ctx {
	zb_zcl_basic_attrs_ext_t basic_attr;
	zb_zcl_identify_attrs_t  identify_attr;

	/* Power configuration */
	zb_uint8_t battery_voltage;
	zb_uint8_t battery_percentage;
	zb_uint8_t battery_size;
	zb_uint8_t battery_quantity;
	zb_uint8_t battery_rated_voltage;
	zb_uint8_t battery_alarm_mask;
	zb_uint8_t battery_voltage_min_threshold;

	/* Temperature measurement */
	zb_int16_t  temp_measure_value;
	zb_int16_t  temp_min_value;
	zb_int16_t  temp_max_value;
	zb_uint16_t temp_tolerance;

	/* Humidity measurement */
	zb_uint16_t hum_measure_value;
	zb_uint16_t hum_min_value;
	zb_uint16_t hum_max_value;
};

static struct zb_device_ctx dev_ctx;

/* Sensor device handle */
static const struct device *sht;

/* Reset button */
#if DT_NODE_EXISTS(RESET_BUTTON_NODE)
static const struct gpio_dt_spec reset_button = GPIO_DT_SPEC_GET(RESET_BUTTON_NODE, gpios);
static struct gpio_callback button_cb_data;
static int64_t button_press_time;
static bool button_pressed_state;  /* Track logical state */
static bool long_press_handled;    /* Already triggered factory reset */
static struct k_work_delayable debounce_work;
static struct k_work_delayable factory_reset_work;

static void factory_reset_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	/* Check if button is still held */
	if (gpio_pin_get_dt(&reset_button) == 1) {
		long_press_handled = true;
		LOG_WRN("Factory reset - erasing Zigbee NVRAM");
		zigbee_erase_persistent_storage(ZB_TRUE);
		sys_reboot(SYS_REBOOT_COLD);
	}
}

static void debounce_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	/* Read the settled state after debounce period */
	int pressed = gpio_pin_get_dt(&reset_button);

	/* Only act if state actually changed */
	if (pressed == button_pressed_state) {
		return;
	}
	button_pressed_state = pressed;

	if (pressed) {
		/* Button pressed - record time and schedule factory reset check */
		button_press_time = k_uptime_get();
		long_press_handled = false;
		k_work_schedule(&factory_reset_work, K_MSEC(BUTTON_FACTORY_RESET_MS));
		LOG_INF("Button pressed - hold 5s for factory reset");
	} else {
		/* Button released - cancel factory reset, check for short press */
		k_work_cancel_delayable(&factory_reset_work);

		if (long_press_handled) {
			LOG_INF("Button released after factory reset");
		} else {
			int64_t hold_time = k_uptime_get() - button_press_time;

			if (hold_time < BUTTON_SHORT_PRESS_MAX_MS) {
				LOG_INF("Short press - forcing sensor read");
				sensor_read_and_update(0);
			} else {
				LOG_INF("Button released after %lld ms (no action)", hold_time);
			}
		}
	}
}

static void button_callback(const struct device *dev, struct gpio_callback *cb,
			    uint32_t pins)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);

	/* On any edge, schedule debounce check - it will read settled state */
	k_work_reschedule(&debounce_work, K_MSEC(BUTTON_DEBOUNCE_MS));
}

static int button_init(void)
{
	int ret;

	if (!gpio_is_ready_dt(&reset_button)) {
		LOG_ERR("Reset button GPIO not ready");
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&reset_button, GPIO_INPUT);
	if (ret < 0) {
		LOG_ERR("Failed to configure reset button: %d", ret);
		return ret;
	}

	ret = gpio_pin_interrupt_configure_dt(&reset_button, GPIO_INT_EDGE_BOTH);
	if (ret < 0) {
		LOG_ERR("Failed to configure button interrupt: %d", ret);
		return ret;
	}

	gpio_init_callback(&button_cb_data, button_callback,
			   BIT(reset_button.pin));
	gpio_add_callback(reset_button.port, &button_cb_data);

	k_work_init_delayable(&debounce_work, debounce_handler);
	k_work_init_delayable(&factory_reset_work, factory_reset_handler);

	/* Initialize state from current pin level */
	button_pressed_state = gpio_pin_get_dt(&reset_button);

	LOG_INF("Reset button ready on P0.31 (initial state: %s)",
		button_pressed_state ? "pressed" : "released");
	return 0;
}
#endif /* DT_NODE_EXISTS(RESET_BUTTON_NODE) */

/* ─── ZCL attribute lists ─── */

ZB_ZCL_DECLARE_BASIC_ATTRIB_LIST_EXT(
	basic_attr_list,
	&dev_ctx.basic_attr.zcl_version,
	&dev_ctx.basic_attr.app_version,
	&dev_ctx.basic_attr.stack_version,
	&dev_ctx.basic_attr.hw_version,
	dev_ctx.basic_attr.mf_name,
	dev_ctx.basic_attr.model_id,
	dev_ctx.basic_attr.date_code,
	&dev_ctx.basic_attr.power_source,
	dev_ctx.basic_attr.location_id,
	&dev_ctx.basic_attr.ph_env,
	dev_ctx.basic_attr.sw_ver);

ZB_ZCL_DECLARE_IDENTIFY_CLIENT_ATTRIB_LIST(
	identify_client_attr_list);

ZB_ZCL_DECLARE_IDENTIFY_SERVER_ATTRIB_LIST(
	identify_server_attr_list,
	&dev_ctx.identify_attr.identify_time);

/* Custom power config attribute list with batteryPercentageRemaining
 * Using raw attribute descriptors since ZBOSS macros require bat_num suffix
 */
zb_zcl_attr_t power_config_attr_list[] = {
	{
		ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_VOLTAGE_ID,
		ZB_ZCL_ATTR_TYPE_U8,
		ZB_ZCL_ATTR_ACCESS_READ_ONLY,
		(ZB_ZCL_NON_MANUFACTURER_SPECIFIC),
		(void *)&dev_ctx.battery_voltage
	},
	{
		ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_PERCENTAGE_REMAINING_ID,
		ZB_ZCL_ATTR_TYPE_U8,
		ZB_ZCL_ATTR_ACCESS_READ_ONLY | ZB_ZCL_ATTR_ACCESS_REPORTING,
		(ZB_ZCL_NON_MANUFACTURER_SPECIFIC),
		(void *)&dev_ctx.battery_percentage
	},
	{
		ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_SIZE_ID,
		ZB_ZCL_ATTR_TYPE_8BIT_ENUM,
		ZB_ZCL_ATTR_ACCESS_READ_ONLY,
		(ZB_ZCL_NON_MANUFACTURER_SPECIFIC),
		(void *)&dev_ctx.battery_size
	},
	{
		ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_QUANTITY_ID,
		ZB_ZCL_ATTR_TYPE_U8,
		ZB_ZCL_ATTR_ACCESS_READ_ONLY,
		(ZB_ZCL_NON_MANUFACTURER_SPECIFIC),
		(void *)&dev_ctx.battery_quantity
	},
	{
		ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_RATED_VOLTAGE_ID,
		ZB_ZCL_ATTR_TYPE_U8,
		ZB_ZCL_ATTR_ACCESS_READ_ONLY,
		(ZB_ZCL_NON_MANUFACTURER_SPECIFIC),
		(void *)&dev_ctx.battery_rated_voltage
	},
	{
		ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_ALARM_MASK_ID,
		ZB_ZCL_ATTR_TYPE_8BITMAP,
		ZB_ZCL_ATTR_ACCESS_READ_WRITE,
		(ZB_ZCL_NON_MANUFACTURER_SPECIFIC),
		(void *)&dev_ctx.battery_alarm_mask
	},
	{
		ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_VOLTAGE_MIN_THRESHOLD_ID,
		ZB_ZCL_ATTR_TYPE_U8,
		ZB_ZCL_ATTR_ACCESS_READ_WRITE,
		(ZB_ZCL_NON_MANUFACTURER_SPECIFIC),
		(void *)&dev_ctx.battery_voltage_min_threshold
	},
	{
		ZB_ZCL_NULL_ID,
		0,
		0,
		(ZB_ZCL_NON_MANUFACTURER_SPECIFIC),
		NULL
	}
};

ZB_ZCL_DECLARE_TEMP_MEASUREMENT_ATTRIB_LIST(
	temp_measurement_attr_list,
	&dev_ctx.temp_measure_value,
	&dev_ctx.temp_min_value,
	&dev_ctx.temp_max_value,
	&dev_ctx.temp_tolerance);

ZB_ZCL_DECLARE_REL_HUMIDITY_MEASUREMENT_ATTRIB_LIST(
	humidity_attr_list,
	&dev_ctx.hum_measure_value,
	&dev_ctx.hum_min_value,
	&dev_ctx.hum_max_value);

/* ─── Cluster list, endpoint, device context ─── */

ZB_DECLARE_FROSTBEE_CLUSTER_LIST(
	frostbee_clusters,
	basic_attr_list,
	identify_client_attr_list,
	identify_server_attr_list,
	power_config_attr_list,
	temp_measurement_attr_list,
	humidity_attr_list);

ZB_DECLARE_FROSTBEE_EP(
	frostbee_ep,
	FROSTBEE_ENDPOINT,
	frostbee_clusters);

ZBOSS_DECLARE_DEVICE_CTX_1_EP(
	frostbee_ctx,
	frostbee_ep);

/* ─── Attribute initialization ─── */

static void clusters_attr_init(void)
{
	/* Basic cluster */
	dev_ctx.basic_attr.zcl_version = ZB_ZCL_VERSION;
	dev_ctx.basic_attr.app_version = FROSTBEE_INIT_BASIC_APP_VERSION;
	dev_ctx.basic_attr.stack_version = FROSTBEE_INIT_BASIC_STACK_VERSION;
	dev_ctx.basic_attr.hw_version = FROSTBEE_INIT_BASIC_HW_VERSION;

	ZB_ZCL_SET_STRING_VAL(
		dev_ctx.basic_attr.mf_name,
		FROSTBEE_INIT_BASIC_MANUF_NAME,
		ZB_ZCL_STRING_CONST_SIZE(FROSTBEE_INIT_BASIC_MANUF_NAME));

	ZB_ZCL_SET_STRING_VAL(
		dev_ctx.basic_attr.model_id,
		FROSTBEE_INIT_BASIC_MODEL_ID,
		ZB_ZCL_STRING_CONST_SIZE(FROSTBEE_INIT_BASIC_MODEL_ID));

	ZB_ZCL_SET_STRING_VAL(
		dev_ctx.basic_attr.date_code,
		FROSTBEE_INIT_BASIC_DATE_CODE,
		ZB_ZCL_STRING_CONST_SIZE(FROSTBEE_INIT_BASIC_DATE_CODE));

	dev_ctx.basic_attr.power_source = ZB_ZCL_BASIC_POWER_SOURCE_BATTERY;

	ZB_ZCL_SET_STRING_VAL(
		dev_ctx.basic_attr.location_id,
		FROSTBEE_INIT_BASIC_LOCATION_DESC,
		ZB_ZCL_STRING_CONST_SIZE(FROSTBEE_INIT_BASIC_LOCATION_DESC));

	dev_ctx.basic_attr.ph_env = FROSTBEE_INIT_BASIC_PH_ENV;

	/* Identify cluster */
	dev_ctx.identify_attr.identify_time =
		ZB_ZCL_IDENTIFY_IDENTIFY_TIME_DEFAULT_VALUE;

	/* Power configuration — AA batteries */
	dev_ctx.battery_voltage = 30;  /* 3.0V in units of 100mV */
	dev_ctx.battery_percentage = 164; /* 82% (ZCL uses 0.5% units, so 164 = 82%) */
	dev_ctx.battery_size = ZB_ZCL_POWER_CONFIG_BATTERY_SIZE_AA;
	dev_ctx.battery_quantity = 2;
	dev_ctx.battery_rated_voltage = 15; /* 1.5V per cell in units of 100mV */
	dev_ctx.battery_alarm_mask = 0;
	dev_ctx.battery_voltage_min_threshold = 20; /* 2.0V alarm threshold */

	/* Temperature measurement */
	dev_ctx.temp_measure_value = ZB_ZCL_ATTR_TEMP_MEASUREMENT_VALUE_UNKNOWN;
	dev_ctx.temp_min_value = FROSTBEE_TEMP_MIN_VALUE;
	dev_ctx.temp_max_value = FROSTBEE_TEMP_MAX_VALUE;
	dev_ctx.temp_tolerance = 20; /* 0.2 C tolerance (SHT40 typical accuracy) */

	/* Humidity measurement */
	dev_ctx.hum_measure_value = ZB_ZCL_ATTR_REL_HUMIDITY_MEASUREMENT_VALUE_UNKNOWN;
	dev_ctx.hum_min_value = FROSTBEE_HUM_MIN_VALUE;
	dev_ctx.hum_max_value = FROSTBEE_HUM_MAX_VALUE;
}

/* ─── Sensor reading & ZCL attribute update ─── */

static void sensor_read_and_update(zb_bufid_t bufid)
{
	struct sensor_value temp, hum;
	int ret;

	ARG_UNUSED(bufid);

	if (!device_is_ready(sht)) {
		LOG_ERR("SHT4X not ready, skipping read");
		goto reschedule;
	}

	ret = sensor_sample_fetch(sht);
	if (ret) {
		LOG_ERR("Sensor fetch failed: %d", ret);
		goto reschedule;
	}

	sensor_channel_get(sht, SENSOR_CHAN_AMBIENT_TEMP, &temp);
	sensor_channel_get(sht, SENSOR_CHAN_HUMIDITY, &hum);

	/* Convert to ZCL format:
	 * Temperature: signed int16 in units of 0.01 C
	 * Humidity: unsigned int16 in units of 0.01 %RH
	 */
	zb_int16_t temp_zcl = (zb_int16_t)(temp.val1 * 100 +
					    temp.val2 / 10000);
	zb_uint16_t hum_zcl = (zb_uint16_t)(hum.val1 * 100 +
					     hum.val2 / 10000);

	LOG_INF("T: %d.%02d C (%d)  H: %d.%02d %%RH (%u)",
		temp.val1, temp.val2 / 10000, temp_zcl,
		hum.val1, hum.val2 / 10000, hum_zcl);

	/* Update ZCL attributes — ZB_FALSE just stores the value.
	 * The ZBOSS reporting engine sends reports automatically
	 * based on the coordinator's Configure Reporting thresholds
	 * (min/max interval, reportable change).
	 */
	ZB_ZCL_SET_ATTRIBUTE(
		FROSTBEE_ENDPOINT,
		ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT,
		ZB_ZCL_CLUSTER_SERVER_ROLE,
		ZB_ZCL_ATTR_TEMP_MEASUREMENT_VALUE_ID,
		(zb_uint8_t *)&temp_zcl,
		ZB_FALSE);

	ZB_ZCL_SET_ATTRIBUTE(
		FROSTBEE_ENDPOINT,
		ZB_ZCL_CLUSTER_ID_REL_HUMIDITY_MEASUREMENT,
		ZB_ZCL_CLUSTER_SERVER_ROLE,
		ZB_ZCL_ATTR_REL_HUMIDITY_MEASUREMENT_VALUE_ID,
		(zb_uint8_t *)&hum_zcl,
		ZB_FALSE);

	/* Battery percentage: static 82% for now (TODO: read actual voltage) */
	ZB_ZCL_SET_ATTRIBUTE(
		FROSTBEE_ENDPOINT,
		ZB_ZCL_CLUSTER_ID_POWER_CONFIG,
		ZB_ZCL_CLUSTER_SERVER_ROLE,
		ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_PERCENTAGE_REMAINING_ID,
		(zb_uint8_t *)&dev_ctx.battery_percentage,
		ZB_FALSE);

reschedule:
	ZB_SCHEDULE_APP_ALARM(sensor_read_and_update, 0,
			      ZB_MILLISECONDS_TO_BEACON_INTERVAL(
				      SENSOR_READ_INTERVAL_S * 1000));
}

/* ─── Zigbee signal handler ─── */

void zboss_signal_handler(zb_bufid_t bufid)
{
	zb_zdo_app_signal_hdr_t *sig_hndler = NULL;
	zb_zdo_app_signal_type_t sig = zb_get_app_signal(bufid, &sig_hndler);
	zb_ret_t status = ZB_GET_APP_SIGNAL_STATUS(bufid);

	switch (sig) {
	case ZB_BDB_SIGNAL_DEVICE_REBOOT:
		/* fall-through */
	case ZB_BDB_SIGNAL_STEERING:
		ZB_ERROR_CHECK(zigbee_default_signal_handler(bufid));
		if (status == RET_OK) {
			LOG_INF("Joined network, starting sensor reads");
			/* Start periodic sensor reading */
			ZB_SCHEDULE_APP_ALARM(sensor_read_and_update, 0,
					      ZB_MILLISECONDS_TO_BEACON_INTERVAL(
						      1000));
		}
		break;

	case ZB_ZDO_SIGNAL_PRODUCTION_CONFIG_READY:
		/* Production config partition is empty - this is normal,
		 * we don't use install codes or pre-shared keys. */
		break;

	case ZB_SIGNAL_JOIN_DONE:
		/* Certification testing signal - ignore. */
		break;

	default:
		ZB_ERROR_CHECK(zigbee_default_signal_handler(bufid));
		break;
	}

	if (bufid) {
		zb_buf_free(bufid);
	}
}

/* ─── Main ─── */

int main(void)
{
	LOG_INF("Frostbee starting - Zigbee SHT40 sensor");

	/* Get sensor device handle */
	sht = DEVICE_DT_GET_ANY(sensirion_sht4x);
	if (sht == NULL) {
		LOG_ERR("SHT4X device not found in devicetree");
		return -ENODEV;
	}
	if (!device_is_ready(sht)) {
		LOG_ERR("SHT4X device not ready");
		return -ENODEV;
	}
	LOG_INF("SHT40 sensor ready");

#if DT_NODE_EXISTS(RESET_BUTTON_NODE)
	if (button_init() < 0) {
		LOG_WRN("Reset button init failed - continuing without it");
	}
#endif

	/* Erase persistent storage if requested */
	zigbee_erase_persistent_storage(ERASE_PERSISTENT_CONFIG);

	/* Configure as sleepy end device */
	zb_set_ed_timeout(ED_AGING_TIMEOUT_64MIN);
	zb_set_keepalive_timeout(ZB_MILLISECONDS_TO_BEACON_INTERVAL(3000));
	zigbee_configure_sleepy_behavior(true);

	/* Power down unused RAM */
	if (IS_ENABLED(CONFIG_RAM_POWER_DOWN_LIBRARY)) {
		power_down_unused_ram();
	}

	/* Register device context and initialize attributes */
	ZB_AF_REGISTER_DEVICE_CTX(&frostbee_ctx);
	clusters_attr_init();

	/* Start Zigbee stack */
	zigbee_enable();

	LOG_INF("Frostbee Zigbee stack started");

	while (1) {
		k_sleep(K_FOREVER);
	}

	return 0;
}
