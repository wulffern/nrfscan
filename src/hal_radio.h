#ifndef HAL_RADIO_H__
#define HAL_RADIO_H__

#include <stdint.h>

/** 1 MHz bins from 2400 MHz: FREQUENCY register 0..79 */
#define RSSI_CHANNEL_COUNT 80
#define RSSI_FREQ_BASE_MHZ 2400

void hal_clock_hfclk_start(void);
void hal_clock_hfclk_stop(void);
void hal_timer_init(void);
uint32_t hal_time_us(void);
void hal_radio_init(void);
void hal_radio_scan_rssi(int8_t *rssi_dbm, uint32_t dwell_us);

#endif
