#ifndef HAL_RADIO_INTERNAL_H__
#define HAL_RADIO_INTERNAL_H__

#include "hal_radio.h"
#include <mdk/nrf.h>
#include <stdbool.h>
#include <stdint.h>

#define HAL_RADIO_MAX_PAYLOAD_LENGTH (31 + 6)
#define HAL_RADIO_EVENT_TIMEOUT_US   2000u

#ifndef SCAN_SETTLE_US
#define SCAN_SETTLE_US 10
#endif

#ifndef SCAN_DISABLE_PERIOD_CH
#define SCAN_DISABLE_PERIOD_CH 10
#endif

#if SCAN_RADIO_2MBIT
#define HAL_RADIO_MODE_SCAN ((RADIO_MODE_MODE_Ble_2Mbit) << RADIO_MODE_MODE_Pos)
#else
#define HAL_RADIO_MODE_SCAN ((RADIO_MODE_MODE_Ble_1Mbit) << RADIO_MODE_MODE_Pos)
#endif

void hal_radio_wait_us(uint32_t us);
bool hal_radio_wait_event(volatile uint32_t *event, uint32_t timeout_us);
void hal_radio_channel_set(uint8_t channel_index);
int8_t hal_radio_rssi_sample_dbm(void);
int8_t hal_radio_rssi_sample(void);

void hal_radio_base_init(void);
void hal_radio_scan_begin(hal_radio_scan_timing_t *timing, int8_t *rssi_dbm);
void hal_radio_scan_channels(int8_t *rssi_dbm, hal_radio_scan_timing_t *timing);
void hal_radio_scan_end(hal_radio_scan_timing_t *timing);

#endif
