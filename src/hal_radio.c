/*********************************************************************
 *  BLE channel RSSI scan (bare-metal radio), from utility-meter
 *  hal_radio channel mapping.
 ********************************************************************/

#include "hal_radio.h"
#include <mdk/nrf.h>
#include <stdbool.h>

#define DD_MAX_PAYLOAD_LENGTH (31 + 6)

#define DEFAULT_RADIO_SHORTS                                             \
    ((RADIO_SHORTS_READY_START_Enabled << RADIO_SHORTS_READY_START_Pos) | \
     (RADIO_SHORTS_END_DISABLE_Enabled << RADIO_SHORTS_END_DISABLE_Pos))

#define CRC_POLYNOMIAL_INIT_SETTINGS ((0x5B << 0) | (0x06 << 8) | (0x00 << 16))

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

    NRF_RADIO->SHORTS = DEFAULT_RADIO_SHORTS;

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
    NRF_RADIO->MODE = ((RADIO_MODE_MODE_Ble_1Mbit) << RADIO_MODE_MODE_Pos) & RADIO_MODE_MODE_Msk;
    NRF_RADIO->TIFS = 150;

    NRF_RADIO->PREFIX0 = access_address[3];
    NRF_RADIO->BASE0 = (((uint32_t)access_address[2]) << 24) | (((uint32_t)access_address[1]) << 16) |
                       (((uint32_t)access_address[0]) << 8);

    NRF_RADIO->CRCINIT = ((uint32_t)crc_seed[0]) | ((uint32_t)crc_seed[1]) << 8 |
                         ((uint32_t)crc_seed[2]) << 16;

    NRF_RADIO->INTENCLR = 0xFFFFFFFF;
}

static int8_t measure_channel_rssi(uint8_t channel, uint32_t dwell_us)
{
    int8_t best = -127;

    radio_channel_set(channel);

    NRF_RADIO->PACKETPTR = (uint32_t)rx_packet;
    NRF_RADIO->EVENTS_READY = 0;
    NRF_RADIO->EVENTS_RSSIEND = 0;
    NRF_RADIO->EVENTS_DISABLED = 0;
    NRF_RADIO->TASKS_RXEN = 1;

    while (NRF_RADIO->EVENTS_READY == 0) {
    }
    NRF_RADIO->EVENTS_READY = 0;

    uint32_t start = hal_time_us();
    while ((uint32_t)(hal_time_us() - start) < dwell_us) {
        NRF_RADIO->EVENTS_RSSIEND = 0;
        NRF_RADIO->TASKS_RSSISTART = 1;
        while (NRF_RADIO->EVENTS_RSSIEND == 0) {
            if ((uint32_t)(hal_time_us() - start) >= dwell_us) {
                break;
            }
        }
        if (NRF_RADIO->EVENTS_RSSIEND != 0) {
            int8_t dbm = rssi_sample_dbm();
            if (dbm > best) {
                best = dbm;
            }
        }
    }

    NRF_RADIO->TASKS_DISABLE = 1;
    while (NRF_RADIO->EVENTS_DISABLED == 0) {
    }
    NRF_RADIO->EVENTS_DISABLED = 0;

    return best;
}

void hal_radio_scan_rssi(int8_t *rssi_dbm, uint32_t dwell_us)
{
    for (uint8_t ch = 0; ch < RSSI_CHANNEL_COUNT; ch++) {
        rssi_dbm[ch] = measure_channel_rssi(ch, dwell_us);
    }
}
