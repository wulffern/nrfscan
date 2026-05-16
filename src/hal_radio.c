/*********************************************************************
 *  RSSI sweep: FREQ -> RXEN -> settle -> RSSI; DISABLE every 2 channels.
 ********************************************************************/

#include "hal_radio.h"
#include <mdk/nrf.h>
#include <stdbool.h>

#define DD_MAX_PAYLOAD_LENGTH (31 + 6)

#define RADIO_SHORTS_BASE (RADIO_SHORTS_READY_START_Enabled << RADIO_SHORTS_READY_START_Pos)
#define RADIO_SHORTS_HOP                                                                   \
    (RADIO_SHORTS_BASE | (RADIO_SHORTS_DISABLED_RXEN_Enabled << RADIO_SHORTS_DISABLED_RXEN_Pos))

#define CRC_POLYNOMIAL_INIT_SETTINGS ((0x5B << 0) | (0x06 << 8) | (0x00 << 16))

#ifndef SCAN_SETTLE_US
#define SCAN_SETTLE_US 10
#endif

#ifndef SCAN_DISABLE_PERIOD_CH
#define SCAN_DISABLE_PERIOD_CH 10
#endif

#if SCAN_RADIO_2MBIT
#define RADIO_MODE_BLE ((RADIO_MODE_MODE_Ble_2Mbit) << RADIO_MODE_MODE_Pos)
#else
#define RADIO_MODE_BLE ((RADIO_MODE_MODE_Ble_1Mbit) << RADIO_MODE_MODE_Pos)
#endif

#define RADIO_EVENT_TIMEOUT_US 2000u

static const uint8_t access_address[4] = {0xD6, 0xBE, 0x89, 0x8E};
static const uint8_t crc_seed[3] = {0x55, 0x55, 0x55};

static uint8_t rx_packet[DD_MAX_PAYLOAD_LENGTH];

static void radio_channel_set(uint8_t channel_index)
{
    NRF_RADIO->FREQUENCY = channel_index;
    NRF_RADIO->DATAWHITEIV = 0;
}

static int8_t rssi_sample_dbm(void)
{
    uint8_t sample = (uint8_t)((NRF_RADIO->RSSISAMPLE & RADIO_RSSISAMPLE_RSSISAMPLE_Msk) >>
                               RADIO_RSSISAMPLE_RSSISAMPLE_Pos);
    return (int8_t)(-(int8_t)sample);
}

static void wait_us(uint32_t us)
{
    uint32_t start = hal_time_us();

    while ((uint32_t)(hal_time_us() - start) < us) {
    }
}

static bool radio_wait_event(volatile uint32_t *event, uint32_t timeout_us)
{
    uint32_t start = hal_time_us();

    while (*event == 0) {
        if ((uint32_t)(hal_time_us() - start) > timeout_us) {
            return false;
        }
    }
    *event = 0;
    return true;
}

static int8_t radio_rssi_sample(void)
{
    NRF_RADIO->EVENTS_RSSIEND = 0;
    NRF_RADIO->TASKS_RSSISTART = 1;
    if (!radio_wait_event(&NRF_RADIO->EVENTS_RSSIEND, RADIO_EVENT_TIMEOUT_US)) {
        return (int8_t)-127;
    }
    return rssi_sample_dbm();
}

/** DISABLE, then RXEN via DISABLED_RXEN short (FREQUENCY must be set first). */
static void radio_disable_rxen(void)
{
    NRF_RADIO->SHORTS = RADIO_SHORTS_HOP;
    NRF_RADIO->EVENTS_DISABLED = 0;
    NRF_RADIO->TASKS_DISABLE = 1;
    (void)radio_wait_event(&NRF_RADIO->EVENTS_DISABLED, RADIO_EVENT_TIMEOUT_US);
    NRF_RADIO->SHORTS = RADIO_SHORTS_BASE;
}

static int8_t radio_measure_channel(uint8_t channel, bool disable_first, bool wait_ready,
                                    hal_radio_scan_timing_t *timing)
{
    uint32_t t0;

    radio_channel_set(channel);

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
        if (!radio_wait_event(&NRF_RADIO->EVENTS_READY, RADIO_EVENT_TIMEOUT_US)) {
            timing->ready_us += hal_time_us() - t0;
            timing->ready_count++;
            return (int8_t)-127;
        }
        timing->ready_us += hal_time_us() - t0;
        timing->ready_count++;
    }

    t0 = hal_time_us();
    wait_us(SCAN_SETTLE_US);
    timing->settle_us += hal_time_us() - t0;

    t0 = hal_time_us();
    const int8_t rssi = radio_rssi_sample();
    timing->rssi_us += hal_time_us() - t0;
    return rssi;
}

void hal_clock_hfclk_start(void)
{
    NRF_CLOCK->EVENTS_HFCLKSTARTED = 0;
    NRF_CLOCK->TASKS_HFCLKSTART = 1;
    while (NRF_CLOCK->EVENTS_HFCLKSTARTED == 0) {
    }
    NRF_CLOCK->EVENTS_HFCLKSTARTED = 0;
}

void hal_clock_hfclk_stop(void)
{
    NRF_CLOCK->TASKS_HFCLKSTOP = 1;
}

void hal_timer_init(void)
{
    NRF_TIMER1->MODE = TIMER_MODE_MODE_Timer;
    NRF_TIMER1->BITMODE = TIMER_BITMODE_BITMODE_24Bit;
    NRF_TIMER1->PRESCALER = 4;
    NRF_TIMER1->TASKS_CLEAR = 1;
    NRF_TIMER1->TASKS_START = 1;
}

uint32_t hal_time_us(void)
{
    NRF_TIMER1->TASKS_CAPTURE[0] = 1;
    return NRF_TIMER1->CC[0];
}

void hal_radio_init(void)
{
    NRF_RADIO->POWER = RADIO_POWER_POWER_Disabled << RADIO_POWER_POWER_Pos;
    NRF_RADIO->POWER = RADIO_POWER_POWER_Enabled << RADIO_POWER_POWER_Pos;

    NRF_RADIO->SHORTS = RADIO_SHORTS_BASE;

    NRF_RADIO->PCNF0 = (((1UL) << RADIO_PCNF0_S0LEN_Pos) & RADIO_PCNF0_S0LEN_Msk) |
                       (((2UL) << RADIO_PCNF0_S1LEN_Pos) & RADIO_PCNF0_S1LEN_Msk) |
                       (((6UL) << RADIO_PCNF0_LFLEN_Pos) & RADIO_PCNF0_LFLEN_Msk);

    NRF_RADIO->PCNF1 =
        (((RADIO_PCNF1_ENDIAN_Little) << RADIO_PCNF1_ENDIAN_Pos) & RADIO_PCNF1_ENDIAN_Msk) |
        (((3UL) << RADIO_PCNF1_BALEN_Pos) & RADIO_PCNF1_BALEN_Msk) |
        (((0UL) << RADIO_PCNF1_STATLEN_Pos) & RADIO_PCNF1_STATLEN_Msk) |
        ((((uint32_t)DD_MAX_PAYLOAD_LENGTH) << RADIO_PCNF1_MAXLEN_Pos) & RADIO_PCNF1_MAXLEN_Msk) |
        ((RADIO_PCNF1_WHITEEN_Enabled << RADIO_PCNF1_WHITEEN_Pos) & RADIO_PCNF1_WHITEEN_Msk);

    NRF_RADIO->CRCPOLY = CRC_POLYNOMIAL_INIT_SETTINGS;
    NRF_RADIO->CRCCNF = (((RADIO_CRCCNF_SKIPADDR_Skip) << RADIO_CRCCNF_SKIPADDR_Pos) &
                         RADIO_CRCCNF_SKIPADDR_Msk) |
                        (((RADIO_CRCCNF_LEN_Three) << RADIO_CRCCNF_LEN_Pos) & RADIO_CRCCNF_LEN_Msk);

    NRF_RADIO->RXADDRESSES = (RADIO_RXADDRESSES_ADDR0_Enabled << RADIO_RXADDRESSES_ADDR0_Pos);
    NRF_RADIO->MODE = RADIO_MODE_BLE & RADIO_MODE_MODE_Msk;
    NRF_RADIO->TIFS = 150;

    NRF_RADIO->PREFIX0 = access_address[3];
    NRF_RADIO->BASE0 = (((uint32_t)access_address[2]) << 24) | (((uint32_t)access_address[1]) << 16) |
                       (((uint32_t)access_address[0]) << 8);

    NRF_RADIO->CRCINIT = ((uint32_t)crc_seed[0]) | ((uint32_t)crc_seed[1]) << 8 |
                         ((uint32_t)crc_seed[2]) << 16;

    NRF_RADIO->INTENCLR = 0xFFFFFFFF;
}

void hal_radio_scan_rssi(int8_t *rssi_dbm, hal_radio_scan_timing_t *timing)
{
    timing->disable_us = 0;
    timing->ready_us = 0;
    timing->settle_us = 0;
    timing->rssi_us = 0;
    timing->final_disable_us = 0;
    timing->disable_count = 0;
    timing->ready_count = 0;

    NRF_RADIO->PACKETPTR = (uint32_t)rx_packet;

    for (uint8_t idx = 0; idx < RSSI_CHANNEL_COUNT; idx++) {
        const uint8_t ch = (uint8_t)(idx * RSSI_FREQ_STEP_MHZ);
        const bool disable_first = (SCAN_DISABLE_PERIOD_CH > 0) && (idx > 0) &&
                                   ((idx % SCAN_DISABLE_PERIOD_CH) == 0);
        const bool wait_ready = (idx == 0) || disable_first;

        rssi_dbm[idx] = radio_measure_channel(ch, disable_first, wait_ready, timing);
    }

    const uint32_t t0 = hal_time_us();
    NRF_RADIO->EVENTS_DISABLED = 0;
    NRF_RADIO->TASKS_DISABLE = 1;
    (void)radio_wait_event(&NRF_RADIO->EVENTS_DISABLED, RADIO_EVENT_TIMEOUT_US);
    timing->final_disable_us = hal_time_us() - t0;
}
