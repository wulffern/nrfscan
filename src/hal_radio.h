#ifndef HAL_RADIO_H__
#define HAL_RADIO_H__

#include <stdint.h>

#ifndef SCAN_RADIO_2MBIT
#define SCAN_RADIO_2MBIT 1
#endif

#define RSSI_FREQ_BASE_MHZ 2400

#if SCAN_RADIO_2MBIT
/** BLE 2 Mbps PHY: 40 bins, 2 MHz steps -> 2400..2478 MHz (FREQUENCY 0,2,..78) */
#define RSSI_FREQ_STEP_MHZ 2
#define RSSI_CHANNEL_COUNT 40
#else
/** BLE 1 Mbps PHY: 80 bins, 1 MHz steps -> 2400..2479 MHz (FREQUENCY 0..79) */
#define RSSI_FREQ_STEP_MHZ 1
#define RSSI_CHANNEL_COUNT 80
#endif

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
void hal_radio_scan_rssi(int8_t *rssi_dbm, hal_radio_scan_timing_t *timing);

#endif
