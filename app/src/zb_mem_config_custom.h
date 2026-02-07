/*
 * Frostbee - ZBOSS Memory Configuration
 *
 * Minimal memory config for Zigbee End Device.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef ZB_MEM_CONFIG_CUSTOM_H
#define ZB_MEM_CONFIG_CUSTOM_H 1

#define ZB_CONFIG_ROLE_ZED
#define ZB_CONFIG_OVERALL_NETWORK_SIZE 16
#define ZB_CONFIG_LIGHT_TRAFFIC
#define ZB_CONFIG_APPLICATION_SIMPLE

#include "zb_mem_config_common.h"
#include "zb_mem_config_context.h"

#endif /* ZB_MEM_CONFIG_CUSTOM_H */
