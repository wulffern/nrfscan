/*********************************************************************
 *  nRF54 RSSI sweep: PLLEN reload per bin (nRF54LM20-class radio).
 ********************************************************************/

#include "hal_radio_internal.h"
#include <mdk/nrf.h>

#define RADIO_SHORTS_PLL_HOP                                                                   \
    (RADIO_SHORTS_PLLREADY_RXEN_Enabled << RADIO_SHORTS_PLLREADY_RXEN_Pos)
#define RADIO_DATAWHITE_IV  0x00890040UL
#define RADIO_PCNF1_RESET   0x00000000UL
#define RADIO_PLLREADY_TIMEOUT_US 150u
#define RADIO_RXREADY_TIMEOUT_US  150u
#define RADIO_SETTLE_US           SCAN_SETTLE_US
#define RADIO_RSSI_SAMPLES          3u

static void radio_constlat_enable(void)
{
    //NRF_POWER->TASKS_CONSTLAT = 1;
}

static void radio_channel_set_nrf54(uint8_t channel_index)
{
    hal_radio_channel_set(channel_index);
    
    //NRF_RADIO->DATAWHITE = RADIO_DATAWHITE_IV;
}

static int8_t radio_rssi_sample_max(void)
{
    NRF_RADIO->TASKS_RSSISTART = 1;
    const int8_t rssi = hal_radio_rssi_sample_dbm();
    return rssi;
}

int8_t hal_radio_rssi_sample(void)
{
    return radio_rssi_sample_max();
}

void hal_radio_init(void)
{
    /* radio_reset() + radio_phy_set() for nRF54LX (Zephyr BLE controller). */
    NRF_RADIO->PCNF1 = RADIO_PCNF1_RESET;
    NRF_RADIO->TIMING =
        (RADIO_TIMING_RU_Fast << RADIO_TIMING_RU_Pos) & RADIO_TIMING_RU_Msk;

    hal_radio_base_init();

    NRF_RADIO->TXPOWER =
        (RADIO_TXPOWER_TXPOWER_0dBm << RADIO_TXPOWER_TXPOWER_Pos) & RADIO_TXPOWER_TXPOWER_Msk;
    NRF_RADIO->INTENCLR00 = 0xFFFFFFFF;

}

static bool radio_pll_reload_rxen(hal_radio_scan_timing_t *timing)
{
    uint32_t t0;

    NRF_RADIO->SHORTS = RADIO_SHORTS_PLL_HOP;
    NRF_RADIO->EVENTS_PLLREADY = 0;
    NRF_RADIO->EVENTS_RXREADY = 0;

    t0 = hal_time_us();
    NRF_RADIO->TASKS_PLLEN = 1;
    //if (!hal_radio_wait_event(&NRF_RADIO->EVENTS_PLLREADY, RADIO_PLLREADY_TIMEOUT_US)) {
    //    NRF_RADIO->SHORTS = 0;
    //    timing->ready_us += hal_time_us() - t0;
    //    timing->ready_count++;
    //    return false;
    // }
    timing->ready_us += hal_time_us() - t0;
    timing->ready_count++;

    //NRF_RADIO->SHORTS = 0;

    //t0 = hal_time_us();
    //if (!hal_radio_wait_event(&NRF_RADIO->EVENTS_RXREADY, RADIO_RXREADY_TIMEOUT_US)) {
    //    timing->ready_us += hal_time_us() - t0;
    //    timing->ready_count++;
    //    return false;
    //}
    //timing->ready_us += hal_time_us() - t0;
    //timing->ready_count++;
    return true;
}

static int8_t radio_measure_channel(uint8_t channel, hal_radio_scan_timing_t *timing)
{
    uint32_t t0;

    radio_channel_set_nrf54(channel);

    if (!radio_pll_reload_rxen(timing)) {
        return (int8_t)-127;
    }

    t0 = hal_time_us();
    hal_radio_wait_us(RADIO_SETTLE_US);
    timing->settle_us += hal_time_us() - t0;


    //  hal_radio_wait_us(30);

    t0 = hal_time_us();
    const int8_t rssi = hal_radio_rssi_sample();
    timing->rssi_us += hal_time_us() - t0;


    return rssi;
}

void hal_radio_scan_channels(int8_t *rssi_dbm, hal_radio_scan_timing_t *timing)
{

        hal_radio_channel_set(0);
    radio_constlat_enable();
    NRF_RADIO->TASKS_RXEN = 0x1;
    hal_radio_wait_us(40);
    NRF_RADIO->TASKS_START = 1;
    hal_radio_wait_us(5);

    for (uint8_t idx = 0; idx < RSSI_CHANNEL_COUNT; idx++) {
        const uint8_t ch = (uint8_t)(idx * RSSI_FREQ_STEP_MHZ);

        rssi_dbm[idx] = radio_measure_channel(ch, timing);
    }
    NRF_RADIO->TASKS_STOP = 1;

}
