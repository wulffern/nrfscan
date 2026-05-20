/*********************************************************************
 *  Shared radio helpers and packet setup (all SoCs).
 ********************************************************************/

#include "hal_radio_internal.h"
#include "hal_platform.h"
#include "hal_soc.h"
#include <mdk/nrf.h>

#define CRC_POLYNOMIAL_INIT_SETTINGS ((0x5B << 0) | (0x06 << 8) | (0x00 << 16))

static const uint8_t access_address[4] = {0xD6, 0xBE, 0x89, 0x8E};
static const uint8_t crc_seed[3] = {0x55, 0x55, 0x55};

static uint8_t rx_packet[HAL_RADIO_MAX_PAYLOAD_LENGTH];

void hal_radio_wait_us(uint32_t us)
{
    uint32_t start = hal_time_us();

    while ((uint32_t)(hal_time_us() - start) < us) {
    }
}

bool hal_radio_wait_event(volatile uint32_t *event, uint32_t timeout_us)
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

void hal_radio_channel_set(uint8_t channel_index)
{
    NRF_RADIO->FREQUENCY = channel_index;
}

int8_t hal_radio_rssi_sample_dbm(void)
{
    uint8_t sample = (uint8_t)((NRF_RADIO->RSSISAMPLE & RADIO_RSSISAMPLE_RSSISAMPLE_Msk) >>
                               RADIO_RSSISAMPLE_RSSISAMPLE_Pos);
    return (int8_t)(-(int8_t)sample);
}

void hal_radio_base_init(void)
{
    NRF_RADIO->SHORTS = 0;

    /* lll_test: 8-bit length; SoC files choose whitening. */
    NRF_RADIO->PCNF0 =
        (((1UL) << RADIO_PCNF0_S0LEN_Pos) & RADIO_PCNF0_S0LEN_Msk) |
        (((8UL) << RADIO_PCNF0_LFLEN_Pos) & RADIO_PCNF0_LFLEN_Msk);

    NRF_RADIO->PCNF1 =
        (((RADIO_PCNF1_ENDIAN_Little) << RADIO_PCNF1_ENDIAN_Pos) & RADIO_PCNF1_ENDIAN_Msk) |
        (((3UL) << RADIO_PCNF1_BALEN_Pos) & RADIO_PCNF1_BALEN_Msk) |
        (((0UL) << RADIO_PCNF1_STATLEN_Pos) & RADIO_PCNF1_STATLEN_Msk) |
        ((((uint32_t)HAL_RADIO_MAX_PAYLOAD_LENGTH) << RADIO_PCNF1_MAXLEN_Pos) &
         RADIO_PCNF1_MAXLEN_Msk);

    NRF_RADIO->CRCPOLY = CRC_POLYNOMIAL_INIT_SETTINGS;
    NRF_RADIO->CRCCNF = (((RADIO_CRCCNF_SKIPADDR_Skip) << RADIO_CRCCNF_SKIPADDR_Pos) &
                         RADIO_CRCCNF_SKIPADDR_Msk) |
                        (((RADIO_CRCCNF_LEN_Three) << RADIO_CRCCNF_LEN_Pos) & RADIO_CRCCNF_LEN_Msk);

    NRF_RADIO->RXADDRESSES = (RADIO_RXADDRESSES_ADDR0_Enabled << RADIO_RXADDRESSES_ADDR0_Pos);
    NRF_RADIO->MODE = HAL_RADIO_MODE_SCAN & RADIO_MODE_MODE_Msk;
    NRF_RADIO->TIFS = 150;

    NRF_RADIO->PREFIX0 = access_address[3];
    NRF_RADIO->BASE0 = (((uint32_t)access_address[2]) << 24) | (((uint32_t)access_address[1]) << 16) |
                       (((uint32_t)access_address[0]) << 8);

    NRF_RADIO->CRCINIT = ((uint32_t)crc_seed[0]) | ((uint32_t)crc_seed[1]) << 8 |
                         ((uint32_t)crc_seed[2]) << 16;
}

void hal_radio_scan_begin(hal_radio_scan_timing_t *timing, int8_t *rssi_dbm)
{
    (void)rssi_dbm;

    timing->hfclk_us = 0;
    timing->disable_us = 0;
    timing->ready_us = 0;
    timing->settle_us = 0;
    timing->rssi_us = 0;
    timing->final_disable_us = 0;
    timing->disable_count = 0;
    timing->ready_count = 0;

    const uint32_t t0 = hal_time_us();
    hal_clock_hfclk_start();
    timing->hfclk_us = hal_time_us() - t0;

    NRF_RADIO->PACKETPTR = (uint32_t)rx_packet;
}

void hal_radio_scan_end(hal_radio_scan_timing_t *timing)
{
    const uint32_t t0 = hal_time_us();

    NRF_RADIO->EVENTS_DISABLED = 0;
    NRF_RADIO->TASKS_DISABLE = 1;
    (void)hal_radio_wait_event(&NRF_RADIO->EVENTS_DISABLED, HAL_RADIO_EVENT_TIMEOUT_US);
    timing->final_disable_us = hal_time_us() - t0;
}

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

void hal_radio_scan_rssi(int8_t *rssi_dbm, hal_radio_scan_timing_t *timing)
{
    hal_radio_scan_begin(timing, rssi_dbm);
    hal_radio_scan_channels(rssi_dbm, timing);
    hal_radio_scan_end(timing);
    hal_clock_hfclk_stop();
}
