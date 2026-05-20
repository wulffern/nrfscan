######################################################################
##        Copyright (c) 2026 Carsten Wulff Software, Norway
######################################################################

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(nrfscan)

# Defaults; overridden by -DSCAN_* from `make` / `west build` via sysbuild cache.
set(SCAN_INTERVAL_MS 1000)
set(SCAN_SETTLE_US 2)
set(SCAN_DISABLE_PERIOD_CH 10)
set(SCAN_RADIO_2MBIT 1)
zephyr_get(SCAN_INTERVAL_MS)
zephyr_get(SCAN_SETTLE_US)
zephyr_get(SCAN_DISABLE_PERIOD_CH)
zephyr_get(SCAN_RADIO_2MBIT)

if(CONFIG_SOC_SERIES_NRF54L)
  set(NRFSCAN_SOC_NRF54 1)
else()
  set(NRFSCAN_SOC_NRF54 0)
endif()

target_compile_definitions(app PRIVATE
  SCAN_INTERVAL_MS=${SCAN_INTERVAL_MS}
  SCAN_SETTLE_US=${SCAN_SETTLE_US}
  SCAN_DISABLE_PERIOD_CH=${SCAN_DISABLE_PERIOD_CH}
  SCAN_RADIO_2MBIT=${SCAN_RADIO_2MBIT}
  NRFSCAN_SOC_NRF54=${NRFSCAN_SOC_NRF54}
)
target_sources(app PRIVATE
  src/main.c
  src/hal_radio.c
  src/hal_platform.c
)
