/*********************************************************************
 *  nrfscan_tx: carrier test source for RSSI scanner validation.
 *
 *  Sweeps the scanner band (1M: reg 0..79, 2M: 0,2,..78), 1 s per channel,
 *  then wraps to 2402 and repeats forever. TXEN only (no packets).
 *  MHz = 2400 + reg. Match TX_RADIO_2MBIT to the scanner.
 *********************************************************************/

#include "../src/hal_platform.h"
#include "../src/hal_soc.h"
#include <mdk/nrf.h>
#include <stdbool.h>
#include <stdint.h>

#ifndef TX_RADIO_2MBIT
#define TX_RADIO_2MBIT 0
#endif

#ifndef TX_DWELL_MS
#define TX_DWELL_MS 1000
#endif

#if TX_RADIO_2MBIT
#define TX_FREQ_REG_START 0
#define TX_FREQ_REG_END 78
#define TX_FREQ_REG_STEP 2
#else
#define TX_FREQ_REG_START 0
#define TX_FREQ_REG_END 79
#define TX_FREQ_REG_STEP 1
#endif

#define TX_CHANNEL_COUNT                                                                       \
    (((TX_FREQ_REG_END - TX_FREQ_REG_START) / TX_FREQ_REG_STEP) + 1)

#if TX_RADIO_2MBIT
#define RADIO_MODE_TX ((RADIO_MODE_MODE_Ble_2Mbit) << RADIO_MODE_MODE_Pos)
#else
#define RADIO_MODE_TX ((RADIO_MODE_MODE_Ble_1Mbit) << RADIO_MODE_MODE_Pos)
#endif

#if NRFSCAN_SOC_NRF54
#define RADIO_SHORTS_PLL_TX                                                                    \
    (RADIO_SHORTS_PLLREADY_TXEN_Enabled << RADIO_SHORTS_PLLREADY_TXEN_Pos)
#define RADIO_EVENT_TIMEOUT_US 2000u
#endif

static void radio_init(void)
{
#if !NRFSCAN_SOC_NRF54
    NRF_RADIO->POWER = RADIO_POWER_POWER_Disabled << RADIO_POWER_POWER_Pos;
    NRF_RADIO->POWER = RADIO_POWER_POWER_Enabled << RADIO_POWER_POWER_Pos;
#endif

    NRF_RADIO->SHORTS = 0;
    NRF_RADIO->TXPOWER = (RADIO_TXPOWER_TXPOWER_0dBm << RADIO_TXPOWER_TXPOWER_Pos);
    NRF_RADIO->MODE = RADIO_MODE_TX & RADIO_MODE_MODE_Msk;
#if NRFSCAN_SOC_NRF54
    NRF_RADIO->TIMING =
        (RADIO_TIMING_RU_Fast << RADIO_TIMING_RU_Pos) & RADIO_TIMING_RU_Msk;
    NRF_RADIO->INTENCLR00 = 0xFFFFFFFF;
#else
    NRF_RADIO->INTENCLR = 0xFFFFFFFF;
#endif
}

#if NRFSCAN_SOC_NRF54
static bool radio_wait_event(volatile uint32_t *event, uint32_t timeout_us)
{
    uint32_t n = 0;

    while (*event == 0) {
        if (n++ > timeout_us) {
            return false;
        }
        for (volatile uint32_t d = 0; d < 64; d++) {
        }
    }
    *event = 0;
    return true;
}
#endif

/** Enable carrier on FREQUENCY reg (MHz = 2400 + reg). */
static void radio_carrier_on(uint8_t freq_reg)
{
    NRF_RADIO->FREQUENCY = freq_reg;
#if NRFSCAN_SOC_NRF54
    NRF_RADIO->SHORTS = RADIO_SHORTS_PLL_TX;
    NRF_RADIO->EVENTS_PLLREADY = 0;
    NRF_RADIO->EVENTS_TXREADY = 0;
    NRF_RADIO->TASKS_PLLEN = 1;
    if (!radio_wait_event(&NRF_RADIO->EVENTS_PLLREADY, RADIO_EVENT_TIMEOUT_US)) {
        NRF_RADIO->SHORTS = 0;
        return;
    }
    NRF_RADIO->SHORTS = 0;
    if (!radio_wait_event(&NRF_RADIO->EVENTS_TXREADY, RADIO_EVENT_TIMEOUT_US)) {
        return;
    }
    NRF_RADIO->TASKS_START = 1;
#else
    NRF_RADIO->EVENTS_READY = 0;
    NRF_RADIO->TASKS_TXEN = 1;
    while (NRF_RADIO->EVENTS_READY == 0) {
    }
    NRF_RADIO->EVENTS_READY = 0;
#endif
}

static void radio_carrier_off(void)
{
    NRF_RADIO->EVENTS_DISABLED = 0;
    NRF_RADIO->TASKS_DISABLE = 1;
    while (NRF_RADIO->EVENTS_DISABLED == 0) {
    }
    NRF_RADIO->EVENTS_DISABLED = 0;
}

int main(void)
{
    hal_dcdc_enable();
    hal_interval_init();
    hal_clock_hfclk_start();
    radio_init();

    uint8_t ch = 0;

    for (;;) {
        const uint8_t freq_reg = (uint8_t)(TX_FREQ_REG_START + (ch * TX_FREQ_REG_STEP));

        radio_carrier_off();
        radio_carrier_on(freq_reg);

        hal_interval_wait_ms(TX_DWELL_MS);

        ch++;
        if (ch >= TX_CHANNEL_COUNT) {
            ch = 0;
        }
    }

    return 0;
}
