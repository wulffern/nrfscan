######################################################################
##        Copyright (c) 2026 Carsten Wulff Software, Norway
######################################################################

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(nrfscan)

if(NOT DEFINED SCAN_DWELL_US)
  set(SCAN_DWELL_US 400)
endif()
if(NOT DEFINED SCAN_INTERVAL_MS)
  set(SCAN_INTERVAL_MS 1000)
endif()

target_compile_definitions(app PRIVATE
  SCAN_DWELL_US=${SCAN_DWELL_US}
  SCAN_INTERVAL_MS=${SCAN_INTERVAL_MS}
)
target_sources(app PRIVATE
  src/main.c
  src/hal_radio.c
)
