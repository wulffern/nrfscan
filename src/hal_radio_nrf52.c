/*********************************************************************
 *  nRF52 RSSI sweep: periodic DISABLE+RXEN, RSSIEND.
 ********************************************************************/

#include "hal_radio_internal.h"
#include <mdk/nrf.h>

#define RADIO_SHORTS_HOP (RADIO_SHORTS_DISABLED_RXEN_Enabled << RADIO_SHORTS_DISABLED_RXEN_Pos)

static void radio_disable_rxen(void)
{
    NRF_RADIO->SHORTS = RADIO_SHORTS_HOP;
    NRF_RADIO->EVENTS_DISABLED = 0;
    NRF_RADIO->TASKS_DISABLE = 1;
    (void)hal_radio_wait_event(&NRF_RADIO->EVENTS_DISABLED, HAL_RADIO_EVENT_TIMEOUT_US);
    NRF_RADIO->SHORTS = 0;
}

int8_t hal_radio_rssi_sample(void)
{
    NRF_RADIO->EVENTS_RSSIEND = 0;
    NRF_RADIO->TASKS_RSSISTART = 1;
    if (!hal_radio_wait_event(&NRF_RADIO->EVENTS_RSSIEND, HAL_RADIO_EVENT_TIMEOUT_US)) {
        return (int8_t)-127;
    }
    return hal_radio_rssi_sample_dbm();
}

void hal_radio_init(void)
{
    NRF_RADIO->POWER = RADIO_POWER_POWER_Disabled << RADIO_POWER_POWER_Pos;
    NRF_RADIO->POWER = RADIO_POWER_POWER_Enabled << RADIO_POWER_POWER_Pos;

    hal_radio_base_init();

    NRF_RADIO->DATAWHITEIV = 0;
    NRF_RADIO->PCNF1 |=
        ((RADIO_PCNF1_WHITEEN_Enabled << RADIO_PCNF1_WHITEEN_Pos) & RADIO_PCNF1_WHITEEN_Msk);
    NRF_RADIO->INTENCLR = 0xFFFFFFFF;
}

static int8_t radio_measure_channel(uint8_t channel, bool disable_first, bool wait_ready,
                                    hal_radio_scan_timing_t *timing)
{
    uint32_t t0;

    hal_radio_channel_set(channel);
    NRF_RADIO->DATAWHITEIV = 0;

    if (disable_first) {
        t0 = hal_time_us();
        radio_disable_rxen();
        timing->disable_us += hal_time_us() - t0;
        timing->disable_count++;
    } else {
        NRF_RADIO->TASKS_RXEN = 1;
    }

    if (wait_ready) {
        NRF_RADIO->EVENTS_READY = 0;
        t0 = hal_time_us();
        if (!hal_radio_wait_event(&NRF_RADIO->EVENTS_READY, HAL_RADIO_EVENT_TIMEOUT_US)) {
            timing->ready_us += hal_time_us() - t0;
            timing->ready_count++;
            return (int8_t)-127;
        }
        timing->ready_us += hal_time_us() - t0;
        timing->ready_count++;
    }

    t0 = hal_time_us();
    hal_radio_wait_us(SCAN_SETTLE_US);
    timing->settle_us += hal_time_us() - t0;

    t0 = hal_time_us();
    const int8_t rssi = hal_radio_rssi_sample();
    timing->rssi_us += hal_time_us() - t0;
    return rssi;
}

void hal_radio_scan_channels(int8_t *rssi_dbm, hal_radio_scan_timing_t *timing)
{
    for (uint8_t idx = 0; idx < RSSI_CHANNEL_COUNT; idx++) {
        const uint8_t ch = (uint8_t)(idx * RSSI_FREQ_STEP_MHZ);
        const bool disable_first = (SCAN_DISABLE_PERIOD_CH > 0) && (idx > 0) &&
                                   ((idx % SCAN_DISABLE_PERIOD_CH) == 0);
        const bool wait_ready = (idx == 0) || disable_first;

        rssi_dbm[idx] = radio_measure_channel(ch, disable_first, wait_ready, timing);
    }
}
