#ifndef HAL_RADIO_H__
#define HAL_RADIO_H__

#include <stdint.h>

/** 2 MHz bins from 2400 MHz: FREQUENCY register 0,2,4..78 -> 2400..2478 MHz */
#define RSSI_CHANNEL_COUNT 40
#define RSSI_FREQ_BASE_MHZ 2400
#define RSSI_FREQ_STEP_MHZ 2

/** Cumulative microseconds inside hal_radio_scan_rssi (TIMER1). */
typedef struct {
    uint32_t disable_us;
    uint32_t ready_us;
    uint32_t settle_us;
    uint32_t rssi_us;
    uint32_t final_disable_us;
    uint16_t disable_count;
    uint16_t ready_count;
} hal_radio_scan_timing_t;

void hal_clock_hfclk_start(void);
void hal_clock_hfclk_stop(void);
void hal_timer_init(void);
uint32_t hal_time_us(void);
void hal_radio_init(void);
void hal_radio_scan_rssi(int8_t *rssi_dbm, uint32_t dwell_us, hal_radio_scan_timing_t *timing);

#endif
