# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(app)

# Each test variant has its own source file.
# The active one is selected via Kconfig in prj.conf.
target_sources_ifdef(CONFIG_TEST_01_GPIO_WIGGLE  app PRIVATE src/01_gpio_wiggle.c)
target_sources_ifdef(CONFIG_TEST_02_I2C_SCAN     app PRIVATE src/02_i2c_scan.c)
target_sources_ifdef(CONFIG_TEST_03_I2C_RAW_100K app PRIVATE src/03_i2c_raw_100k.c)
target_sources_ifdef(CONFIG_TEST_04_I2C_RAW_400K app PRIVATE src/04_i2c_raw_400k.c)
target_sources_ifdef(CONFIG_TEST_05_SOFT_RESET   app PRIVATE src/05_soft_reset.c)
target_sources_ifdef(CONFIG_TEST_06_ZEPHYR_DRIVER app PRIVATE src/06_zephyr_driver.c)
target_sources_ifdef(CONFIG_TEST_07_NRFX_TWIM    app PRIVATE src/07_nrfx_twim.c)
target_sources_ifdef(CONFIG_TEST_08_ALT_PINS     app PRIVATE src/08_alt_pins.c)
target_sources_ifdef(CONFIG_TEST_09_VERBOSE       app PRIVATE src/09_verbose.c)
