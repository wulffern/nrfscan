/*********************************************************************
 *  RSSI sweep: FREQ -> RXEN -> settle -> RSSI.
 *  nRF52: periodic DISABLE+RXEN every SCAN_DISABLE_PERIOD_CH bins.
 *  nRF54: per bin FREQUENCY + PLLEN + PLLREADY_RXEN + RXREADY (nRF54LM20).
 ********************************************************************/

#include "hal_radio.h"
#include "hal_soc.h"
#include <mdk/nrf.h>
#include <stdbool.h>

#define DD_MAX_PAYLOAD_LENGTH (31 + 6)

#define RADIO_SHORTS_HOP (RADIO_SHORTS_DISABLED_RXEN_Enabled << RADIO_SHORTS_DISABLED_RXEN_Pos)

#if NRFSCAN_SOC_NRF54
#define RADIO_SHORTS_PLL_HOP                                                                   \
    (RADIO_SHORTS_PLLREADY_RXEN_Enabled << RADIO_SHORTS_PLLREADY_RXEN_Pos)
/* PLL reload + fast 2M RX ramp (~41 us); margin for both. */
#define RADIO_PLLREADY_TIMEOUT_US 150u
#define RADIO_RXREADY_TIMEOUT_US  150u
#endif

#define CRC_POLYNOMIAL_INIT_SETTINGS ((0x5B << 0) | (0x06 << 8) | (0x00 << 16))

#ifndef SCAN_SETTLE_US
#define SCAN_SETTLE_US 10
#endif

#ifndef SCAN_DISABLE_PERIOD_CH
#define SCAN_DISABLE_PERIOD_CH 10
#endif

#if SCAN_RADIO_2MBIT
#define RADIO_MODE_SCAN ((RADIO_MODE_MODE_Ble_2Mbit) << RADIO_MODE_MODE_Pos)
#else
#define RADIO_MODE_SCAN ((RADIO_MODE_MODE_Ble_1Mbit) << RADIO_MODE_MODE_Pos)
#endif

#define RADIO_EVENT_TIMEOUT_US 2000u

#if NRFSCAN_SOC_NRF54
/* Zephyr BLE controller radio_reset() values (radio_nrf54lx.h). */
#define RADIO_DATAWHITE_IV  0x00890040UL
#define RADIO_PCNF1_RESET   0x00000000UL
#define RADIO_SETTLE_US     ((SCAN_SETTLE_US < 100) ? 100U : (uint32_t)SCAN_SETTLE_US)
#define RADIO_RSSI_SAMPLES  3u
#else
#define RADIO_SETTLE_US     ((uint32_t)SCAN_SETTLE_US)
#endif

static const uint8_t access_address[4] = {0xD6, 0xBE, 0x89, 0x8E};
static const uint8_t crc_seed[3] = {0x55, 0x55, 0x55};

static uint8_t rx_packet[DD_MAX_PAYLOAD_LENGTH];

static void radio_channel_set(uint8_t channel_index)
{
    NRF_RADIO->FREQUENCY = channel_index;
#if NRFSCAN_SOC_NRF54
    NRF_RADIO->DATAWHITE = RADIO_DATAWHITE_IV;
#else
    NRF_RADIO->DATAWHITEIV = 0;
#endif
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

#if NRFSCAN_SOC_NRF54
static int8_t radio_rssi_sample_max(void)
{
    int8_t best = (int8_t)-127;

    for (uint32_t n = 0; n < RADIO_RSSI_SAMPLES; n++) {
        NRF_RADIO->TASKS_RSSISTART = 1;
        wait_us(5);
        const int8_t rssi = rssi_sample_dbm();
        if (rssi > best) {
            best = rssi;
        }
    }
    return best;
}
#endif

static int8_t radio_rssi_sample(void)
{
#if NRFSCAN_SOC_NRF54
    return radio_rssi_sample_max();
#else
    NRF_RADIO->EVENTS_RSSIEND = 0;
    NRF_RADIO->TASKS_RSSISTART = 1;
    if (!radio_wait_event(&NRF_RADIO->EVENTS_RSSIEND, RADIO_EVENT_TIMEOUT_US)) {
        return (int8_t)-127;
    }
    return rssi_sample_dbm();
#endif
}

/** DISABLE, then RXEN via DISABLED_RXEN short (FREQUENCY must be set first). */
static void radio_disable_rxen(void)
{
    NRF_RADIO->SHORTS = RADIO_SHORTS_HOP;
    NRF_RADIO->EVENTS_DISABLED = 0;
    NRF_RADIO->TASKS_DISABLE = 1;
    (void)radio_wait_event(&NRF_RADIO->EVENTS_DISABLED, RADIO_EVENT_TIMEOUT_US);
    NRF_RADIO->SHORTS = 0;
}

#if NRFSCAN_SOC_NRF54

static void radio_constlat_enable(void)
{
    NRF_POWER->TASKS_CONSTLAT = 1;
}

static bool radio_pll_reload_rxen(hal_radio_scan_timing_t *timing)
{
    uint32_t t0;

    NRF_RADIO->SHORTS = RADIO_SHORTS_PLL_HOP;
    NRF_RADIO->EVENTS_PLLREADY = 0;
    NRF_RADIO->EVENTS_RXREADY = 0;

    t0 = hal_time_us();
    NRF_RADIO->TASKS_PLLEN = 1;
    if (!radio_wait_event(&NRF_RADIO->EVENTS_PLLREADY, RADIO_PLLREADY_TIMEOUT_US)) {
        NRF_RADIO->SHORTS = 0;
        timing->ready_us += hal_time_us() - t0;
        timing->ready_count++;
        return false;
    }
    timing->ready_us += hal_time_us() - t0;
    timing->ready_count++;

    NRF_RADIO->SHORTS = 0;

    t0 = hal_time_us();
    if (!radio_wait_event(&NRF_RADIO->EVENTS_RXREADY, RADIO_RXREADY_TIMEOUT_US)) {
        timing->ready_us += hal_time_us() - t0;
        timing->ready_count++;
        return false;
    }
    timing->ready_us += hal_time_us() - t0;
    timing->ready_count++;
    return true;
}

/** FREQUENCY -> PLLEN (reload) -> PLLREADY -> RXEN -> RXREADY -> settle -> RSSI. */
static int8_t radio_measure_channel_nrf54(uint8_t channel, hal_radio_scan_timing_t *timing)
{
    uint32_t t0;

    radio_channel_set(channel);

    if (!radio_pll_reload_rxen(timing)) {
        return (int8_t)-127;
    }

    t0 = hal_time_us();
    wait_us(RADIO_SETTLE_US);
    timing->settle_us += hal_time_us() - t0;

    /* LL DTM/RX: receiver active before RSSISTART (instant sample on nRF54). */
    NRF_RADIO->TASKS_START = 1;
    wait_us(30);

    t0 = hal_time_us();
    const int8_t rssi = radio_rssi_sample();
    timing->rssi_us += hal_time_us() - t0;

    NRF_RADIO->TASKS_STOP = 1;
    return rssi;
}

#else

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

#endif

void hal_timer_init(void)
{
    HAL_TIMER->MODE = TIMER_MODE_MODE_Timer;
    HAL_TIMER->BITMODE = TIMER_BITMODE_BITMODE_24Bit;
    HAL_TIMER->PRESCALER = 4;
    HAL_TIMER->TASKS_CLEAR = 1;
    HAL_TIMER->TASKS_START = 1;
}

uint32_t hal_time_us(void)
{
    HAL_TIMER->TASKS_CAPTURE[0] = 1;
    return HAL_TIMER->CC[0];
}

void hal_radio_init(void)
{
#if !NRFSCAN_SOC_NRF54
    NRF_RADIO->POWER = RADIO_POWER_POWER_Disabled << RADIO_POWER_POWER_Pos;
    NRF_RADIO->POWER = RADIO_POWER_POWER_Enabled << RADIO_POWER_POWER_Pos;
#endif

    NRF_RADIO->SHORTS = 0;

#if NRFSCAN_SOC_NRF54
    /* radio_reset() + radio_phy_set() for nRF54LX (Zephyr BLE controller). */
    NRF_RADIO->PCNF1 = RADIO_PCNF1_RESET;
    NRF_RADIO->TIMING =
        (RADIO_TIMING_RU_Fast << RADIO_TIMING_RU_Pos) & RADIO_TIMING_RU_Msk;
#endif

    /* lll_test: 8-bit length, no whitening in test mode. */
    NRF_RADIO->PCNF0 =
        (((1UL) << RADIO_PCNF0_S0LEN_Pos) & RADIO_PCNF0_S0LEN_Msk) |
        (((8UL) << RADIO_PCNF0_LFLEN_Pos) & RADIO_PCNF0_LFLEN_Msk);

    NRF_RADIO->PCNF1 =
        (((RADIO_PCNF1_ENDIAN_Little) << RADIO_PCNF1_ENDIAN_Pos) & RADIO_PCNF1_ENDIAN_Msk) |
        (((3UL) << RADIO_PCNF1_BALEN_Pos) & RADIO_PCNF1_BALEN_Msk) |
        (((0UL) << RADIO_PCNF1_STATLEN_Pos) & RADIO_PCNF1_STATLEN_Msk) |
        ((((uint32_t)DD_MAX_PAYLOAD_LENGTH) << RADIO_PCNF1_MAXLEN_Pos) & RADIO_PCNF1_MAXLEN_Msk);
#if !NRFSCAN_SOC_NRF54
    NRF_RADIO->PCNF1 |=
        ((RADIO_PCNF1_WHITEEN_Enabled << RADIO_PCNF1_WHITEEN_Pos) & RADIO_PCNF1_WHITEEN_Msk);
#endif

    NRF_RADIO->CRCPOLY = CRC_POLYNOMIAL_INIT_SETTINGS;
    NRF_RADIO->CRCCNF = (((RADIO_CRCCNF_SKIPADDR_Skip) << RADIO_CRCCNF_SKIPADDR_Pos) &
                         RADIO_CRCCNF_SKIPADDR_Msk) |
                        (((RADIO_CRCCNF_LEN_Three) << RADIO_CRCCNF_LEN_Pos) & RADIO_CRCCNF_LEN_Msk);

    NRF_RADIO->RXADDRESSES = (RADIO_RXADDRESSES_ADDR0_Enabled << RADIO_RXADDRESSES_ADDR0_Pos);
    NRF_RADIO->MODE = RADIO_MODE_SCAN & RADIO_MODE_MODE_Msk;
    NRF_RADIO->TIFS = 150;

#if NRFSCAN_SOC_NRF54
    NRF_RADIO->TXPOWER =
        (RADIO_TXPOWER_TXPOWER_0dBm << RADIO_TXPOWER_TXPOWER_Pos) & RADIO_TXPOWER_TXPOWER_Msk;
#endif

    NRF_RADIO->PREFIX0 = access_address[3];
    NRF_RADIO->BASE0 = (((uint32_t)access_address[2]) << 24) | (((uint32_t)access_address[1]) << 16) |
                       (((uint32_t)access_address[0]) << 8);

    NRF_RADIO->CRCINIT = ((uint32_t)crc_seed[0]) | ((uint32_t)crc_seed[1]) << 8 |
                         ((uint32_t)crc_seed[2]) << 16;

#if NRFSCAN_SOC_NRF54
    NRF_RADIO->INTENCLR00 = 0xFFFFFFFF;
#else
    NRF_RADIO->INTENCLR = 0xFFFFFFFF;
#endif
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

#if NRFSCAN_SOC_NRF54
    radio_constlat_enable();
    for (uint8_t idx = 0; idx < RSSI_CHANNEL_COUNT; idx++) {
        const uint8_t ch = (uint8_t)(idx * RSSI_FREQ_STEP_MHZ);

        rssi_dbm[idx] = radio_measure_channel_nrf54(ch, timing);
    }
#else
    for (uint8_t idx = 0; idx < RSSI_CHANNEL_COUNT; idx++) {
        const uint8_t ch = (uint8_t)(idx * RSSI_FREQ_STEP_MHZ);
        const bool disable_first = (SCAN_DISABLE_PERIOD_CH > 0) && (idx > 0) &&
                                   ((idx % SCAN_DISABLE_PERIOD_CH) == 0);
        const bool wait_ready = (idx == 0) || disable_first;

        rssi_dbm[idx] = radio_measure_channel(ch, disable_first, wait_ready, timing);
    }
#endif

    const uint32_t t0 = hal_time_us();
    NRF_RADIO->EVENTS_DISABLED = 0;
    NRF_RADIO->TASKS_DISABLE = 1;
    (void)radio_wait_event(&NRF_RADIO->EVENTS_DISABLED, RADIO_EVENT_TIMEOUT_US);
    timing->final_disable_us = hal_time_us() - t0;
}
