######################################################################
##        Copyright (c) 2026 Carsten Wulff Software, Norway
######################################################################

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(nrfscan_tx)

set(TX_RADIO_2MBIT 0)
set(TX_DWELL_MS 1000)
set(TX_FREQ_REG_START 2)
set(TX_FREQ_REG_END 80)
set(TX_FREQ_REG_STEP 2)
zephyr_get(TX_RADIO_2MBIT)
zephyr_get(TX_DWELL_MS)
zephyr_get(TX_FREQ_REG_START)
zephyr_get(TX_FREQ_REG_END)
zephyr_get(TX_FREQ_REG_STEP)

if(CONFIG_SOC_SERIES_NRF54L)
  set(NRFSCAN_SOC_NRF54 1)
else()
  set(NRFSCAN_SOC_NRF54 0)
endif()

target_compile_definitions(app PRIVATE
  TX_RADIO_2MBIT=${TX_RADIO_2MBIT}
  TX_DWELL_MS=${TX_DWELL_MS}
  TX_FREQ_REG_START=${TX_FREQ_REG_START}
  TX_FREQ_REG_END=${TX_FREQ_REG_END}
  TX_FREQ_REG_STEP=${TX_FREQ_REG_STEP}
  NRFSCAN_SOC_NRF54=${NRFSCAN_SOC_NRF54}
)
target_sources(app PRIVATE
  src/main.c
  ../src/hal_platform.c
)
target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
