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
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>
#include <ram_pwrdn.h>

#include <zboss_api.h>
#include <zboss_api_addons.h>
#include <zigbee/zigbee_app_utils.h>
#include <zigbee/zigbee_error_handler.h>
#include <zb_nrf_platform.h>
#include "zb_mem_config_custom.h"
#include "zb_frostbee.h"

LOG_MODULE_REGISTER(frostbee, LOG_LEVEL_INF);

/* Sensor read interval in seconds (used for ZBOSS alarm scheduling). */
#define SENSOR_READ_INTERVAL_S  600

/* Erase NVRAM on every boot so the device rejoins the network.
 * Safe for development — no stale network state, and combined with
 * the dev pm_static.yml (no NVRAM partitions) the Zigbee stack
 * has nowhere to write persistent data.
 * Change to ZB_FALSE (and use pm_static_release.yml) for production.
 */
#define ERASE_PERSISTENT_CONFIG ZB_TRUE

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

ZB_ZCL_DECLARE_POWER_CONFIG_ATTRIB_LIST(
	power_config_attr_list,
	&dev_ctx.battery_voltage,
	&dev_ctx.battery_size,
	&dev_ctx.battery_quantity,
	&dev_ctx.battery_rated_voltage,
	&dev_ctx.battery_alarm_mask,
	&dev_ctx.battery_voltage_min_threshold);

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
	dev_ctx.battery_percentage = 200; /* 100% (ZCL uses 0.5% units, so 200 = 100%) */
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

	/* Update ZCL attributes — the ZBOSS reporting engine will
	 * automatically send reports if the value changed enough
	 * (per the coordinator's Configure Reporting settings).
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
