/*********************************************************************
 *  nrfscan_tx: carrier test source for RSSI scanner validation.
 *
 *  Sweeps the scanner band (1M: reg 0..79, 2M: 0,2,..78), 1 s per channel,
 *  then wraps to 2402 and repeats forever. TXEN only (no packets).
 *  MHz = 2400 + reg. Match TX_RADIO_2MBIT to the scanner.
 *********************************************************************/

#include <mdk/nrf.h>
#include <stdbool.h>
#include <stdint.h>
#include <zephyr/irq.h>

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
#define RADIO_MODE_BLE ((RADIO_MODE_MODE_Ble_2Mbit) << RADIO_MODE_MODE_Pos)
#else
#define RADIO_MODE_BLE ((RADIO_MODE_MODE_Ble_1Mbit) << RADIO_MODE_MODE_Pos)
#endif

#define RTC_TICKS_PER_SEC 32768U
#define RTC_COUNTER_MASK 0xFFFFFFU
#define RTC_CC_CHANNEL 0

static volatile bool rtc_compare_wake;

static void rtc2_compare_isr(const void *arg);

static inline void cpu_wfe(void)
{
    __WFE();
    __SEV();
    __WFE();
}

static void hfclk_start(void)
{
    NRF_CLOCK->EVENTS_HFCLKSTARTED = 0;
    NRF_CLOCK->TASKS_HFCLKSTART = 1;
    while (NRF_CLOCK->EVENTS_HFCLKSTARTED == 0) {
    }
    NRF_CLOCK->EVENTS_HFCLKSTARTED = 0;
}

static void radio_init(void)
{
    NRF_RADIO->POWER = RADIO_POWER_POWER_Disabled << RADIO_POWER_POWER_Pos;
    NRF_RADIO->POWER = RADIO_POWER_POWER_Enabled << RADIO_POWER_POWER_Pos;

    NRF_RADIO->SHORTS = 0;
    NRF_RADIO->TXPOWER = (RADIO_TXPOWER_TXPOWER_0dBm << RADIO_TXPOWER_TXPOWER_Pos);
    NRF_RADIO->MODE = RADIO_MODE_BLE & RADIO_MODE_MODE_Msk;
    NRF_RADIO->INTENCLR = 0xFFFFFFFF;
}

/** Enable carrier on FREQUENCY reg (MHz = 2400 + reg); wait until READY. */
static void radio_carrier_on(uint8_t freq_reg)
{
    NRF_RADIO->FREQUENCY = freq_reg;
    NRF_RADIO->EVENTS_READY = 0;
    NRF_RADIO->TASKS_TXEN = 1;
    while (NRF_RADIO->EVENTS_READY == 0) {
    }
    NRF_RADIO->EVENTS_READY = 0;
}

static void radio_carrier_off(void)
{
    NRF_RADIO->EVENTS_DISABLED = 0;
    NRF_RADIO->TASKS_DISABLE = 1;
    while (NRF_RADIO->EVENTS_DISABLED == 0) {
    }
    NRF_RADIO->EVENTS_DISABLED = 0;
}

static void lfclk_start(void)
{
    if ((NRF_CLOCK->LFCLKSTAT & CLOCK_LFCLKSTAT_STATE_Msk) ==
        (CLOCK_LFCLKSTAT_STATE_Running << CLOCK_LFCLKSTAT_STATE_Pos)) {
        return;
    }

    NRF_CLOCK->LFCLKSRC = CLOCK_LFCLKSRC_SRC_RC << CLOCK_LFCLKSRC_SRC_Pos;
    NRF_CLOCK->EVENTS_LFCLKSTARTED = 0;
    NRF_CLOCK->TASKS_LFCLKSTART = 1;
    while (NRF_CLOCK->EVENTS_LFCLKSTARTED == 0) {
    }
    NRF_CLOCK->EVENTS_LFCLKSTARTED = 0;
}

static void rtc_init(void)
{
    lfclk_start();
    NRF_RTC2->TASKS_STOP = 1;
    NRF_RTC2->TASKS_CLEAR = 1;
    NRF_RTC2->PRESCALER = 0;
    NRF_RTC2->EVTENCLR = 0xFFFFFFFF;
    NRF_RTC2->INTENCLR = 0xFFFFFFFF;
    NRF_RTC2->EVENTS_COMPARE[RTC_CC_CHANNEL] = 0;
    NRF_RTC2->TASKS_START = 1;

    rtc_compare_wake = false;
    IRQ_CONNECT(RTC2_IRQn, 6, rtc2_compare_isr, NULL, 0);
    irq_enable(RTC2_IRQn);
}

static uint32_t rtc_ms_to_ticks(uint32_t ms)
{
    return (RTC_TICKS_PER_SEC * ms) / 1000U;
}

static uint32_t rtc_ticks_delta(uint32_t from, uint32_t to)
{
    return (to - from) & RTC_COUNTER_MASK;
}

static void rtc_schedule_compare_after(uint32_t ticks)
{
    uint32_t now = NRF_RTC2->COUNTER;
    uint32_t target = (now + ticks) & RTC_COUNTER_MASK;

    while (rtc_ticks_delta(now, target) > ticks) {
        now = NRF_RTC2->COUNTER;
        target = (now + ticks) & RTC_COUNTER_MASK;
    }

    rtc_compare_wake = false;
    NRF_RTC2->EVENTS_COMPARE[RTC_CC_CHANNEL] = 0;
    NRF_RTC2->CC[RTC_CC_CHANNEL] = target;
    NRF_RTC2->INTENSET = RTC_INTENSET_COMPARE0_Msk;
}

static void rtc_wait_compare(void)
{
    while (!rtc_compare_wake) {
        cpu_wfe();
    }
    rtc_compare_wake = false;
}

static void rtc2_compare_isr(const void *arg)
{
    ARG_UNUSED(arg);

    if (NRF_RTC2->EVENTS_COMPARE[RTC_CC_CHANNEL] == 0) {
        return;
    }

    NRF_RTC2->EVENTS_COMPARE[RTC_CC_CHANNEL] = 0;
    NRF_RTC2->INTENCLR = RTC_INTENCLR_COMPARE0_Msk;
    rtc_compare_wake = true;
}

int main(void)
{
    NRF_POWER->DCDCEN = 1;

    rtc_init();
    hfclk_start();
    radio_init();

    const uint32_t dwell_ticks = rtc_ms_to_ticks(TX_DWELL_MS);
    uint8_t ch = 0;

    for (;;) {
        const uint8_t freq_reg = (uint8_t)(TX_FREQ_REG_START + (ch * TX_FREQ_REG_STEP));

        radio_carrier_off();
        radio_carrier_on(freq_reg);

        rtc_schedule_compare_after(dwell_ticks);
        rtc_wait_compare();

        ch++;
        if (ch >= TX_CHANNEL_COUNT) {
            ch = 0;
        }
    }

    return 0;
}
