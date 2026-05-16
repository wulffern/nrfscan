/*********************************************************************
 *  nrfscan: sweep RSSI on all BLE channels, JSON on UART every 1 s.
 *********************************************************************/

#include "hal_radio.h"
#include <mdk/nrf.h>
#include <nrfx_uarte.h>
#include <stdbool.h>
#include <stdint.h>
#include <zephyr/irq.h>

#ifndef TXD_PIN
#define TXD_PIN 6
#endif

#ifndef RXD_PIN
#define RXD_PIN 8
#endif

#ifndef SCAN_INTERVAL_MS
#define SCAN_INTERVAL_MS 1000
#endif

#ifndef SCAN_SETTLE_US
#define SCAN_SETTLE_US 10
#endif

#ifndef SCAN_DISABLE_PERIOD_CH
#define SCAN_DISABLE_PERIOD_CH 10
#endif

#ifndef SCAN_RADIO_2MBIT
#define SCAN_RADIO_2MBIT 1
#endif

#define RTC_TICKS_PER_SEC 32768U
#define RTC_COUNTER_MASK 0xFFFFFFU
#define RTC_CC_CHANNEL 0

static volatile bool rtc_compare_wake;

static void rtc2_compare_isr(const void *arg);

/* Waits for the next NVIC event. */
static inline void cpu_wfe(void)
{
    __WFE();
    __SEV();
    __WFE();
}

static nrfx_uarte_t uart = NRFX_UARTE_INSTANCE(NRF_UARTE0);

static void uart_init(void)
{
    nrfx_uarte_config_t config = NRFX_UARTE_DEFAULT_CONFIG(TXD_PIN, RXD_PIN);
    config.baudrate = NRF_UARTE_BAUDRATE_115200;
    nrfx_uarte_init(&uart, &config, NULL);
}

static void uart_put_char(uint8_t ch)
{
    nrfx_uarte_tx(&uart, &ch, 1, NRFX_UARTE_TX_BLOCKING);
}

static void uart_put_string(const char *string)
{
    while (*string != '\0') {
        uart_put_char(*string++);
    }
}

static void uart_put_int(int value)
{
    char buf[12];
    int idx = 0;
    bool neg = false;

    if (value < 0) {
        neg = true;
        value = -value;
    }
    if (value == 0) {
        uart_put_char('0');
        return;
    }
    while (value > 0 && idx < (int)sizeof(buf)) {
        buf[idx++] = (char)('0' + (value % 10));
        value /= 10;
    }
    if (neg) {
        uart_put_char('-');
    }
    while (idx > 0) {
        uart_put_char((uint8_t)buf[--idx]);
    }
}

static void int_to_json(const char *key, int value)
{
    uart_put_string("\"");
    uart_put_string(key);
    uart_put_string("\" : ");
    uart_put_int(value);
}

static void int8_array_to_json(const char *key, const int8_t *array, uint32_t length)
{
    uart_put_string("\"");
    uart_put_string(key);
    uart_put_string("\":[");
    for (uint32_t i = 0; i < length; i++) {
        uart_put_int((int)array[i]);
        if ((i + 1) < length) {
            uart_put_string(",");
        }
    }
    uart_put_string("]");
}

static void scan_report_to_json(const int8_t *rssi_dbm, uint32_t scan_duration_us, uint32_t hfclk_us,
                                const hal_radio_scan_timing_t *timing, uint32_t scan_count)
{
    const uint32_t sweep_accounted = timing->disable_us + timing->ready_us + timing->settle_us +
                                     timing->rssi_us + timing->final_disable_us;

    uart_put_string("{");
    int8_array_to_json("rssi_dB", rssi_dbm, RSSI_CHANNEL_COUNT);
    uart_put_string(",");
    int_to_json("scan_duration_us", (int)scan_duration_us);
    uart_put_char(',');
    int_to_json("time_hfclk_us", (int)hfclk_us);
    uart_put_char(',');
    int_to_json("time_ready_us", (int)timing->ready_us);
    uart_put_char(',');
    int_to_json("time_disable_us", (int)timing->disable_us);
    uart_put_char(',');
    int_to_json("time_settle_us", (int)timing->settle_us);
    uart_put_char(',');
    int_to_json("time_rssi_us", (int)timing->rssi_us);
    uart_put_char(',');
    int_to_json("time_final_disable_us", (int)timing->final_disable_us);
    uart_put_char(',');
    int_to_json("time_other_us", (int)(scan_duration_us - sweep_accounted));
    uart_put_char(',');
    int_to_json("count_ready", (int)timing->ready_count);
    uart_put_char(',');
    int_to_json("count_disable", (int)timing->disable_count);
    uart_put_char(',');
    int_to_json("channels", RSSI_CHANNEL_COUNT);
    uart_put_char(',');
    int_to_json("freq_base_mhz", RSSI_FREQ_BASE_MHZ);
    uart_put_char(',');
    int_to_json("freq_step_mhz", RSSI_FREQ_STEP_MHZ);
    uart_put_char(',');
    int_to_json("interval_ms", SCAN_INTERVAL_MS);
    uart_put_char(',');
    int_to_json("settle_us", SCAN_SETTLE_US);
    uart_put_char(',');
    int_to_json("disable_period_ch", SCAN_DISABLE_PERIOD_CH);
    uart_put_char(',');
    int_to_json("radio_2mbit", SCAN_RADIO_2MBIT);
    uart_put_char(',');
    int_to_json("scan_count", (int)scan_count);
    uart_put_string("}\n\r");
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

/** Arm CC[0] to fire after `ticks` LFCLK ticks (24-bit wrap-safe). */
static void rtc_schedule_compare_after(uint32_t ticks)
{
    uint32_t now = NRF_RTC2->COUNTER;
    uint32_t target = (now + ticks) & RTC_COUNTER_MASK;

    /* If the compare point was already passed, push it forward. */
    while (rtc_ticks_delta(now, target) > ticks) {
        now = NRF_RTC2->COUNTER;
        target = (now + ticks) & RTC_COUNTER_MASK;
    }

    rtc_compare_wake = false;
    NRF_RTC2->EVENTS_COMPARE[RTC_CC_CHANNEL] = 0;
    NRF_RTC2->CC[RTC_CC_CHANNEL] = target;
    NRF_RTC2->INTENSET = RTC_INTENSET_COMPARE0_Msk;
}

/** Sleep until RTC2 CC[0] compare interrupt sets rtc_compare_wake. */
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

static void run_one_scan(int8_t *rssi_dbm, uint32_t *duration_us, uint32_t *hfclk_us,
                         hal_radio_scan_timing_t *timing)
{
    const uint32_t hfclk_start = hal_time_us();
    hal_clock_hfclk_start();
    *hfclk_us = hal_time_us() - hfclk_start;

    const uint32_t scan_start = hal_time_us();
    hal_radio_scan_rssi(rssi_dbm, timing);
    *duration_us = hal_time_us() - scan_start;
    hal_clock_hfclk_stop();
}

int main(void)
{
    int8_t rssi_dbm[RSSI_CHANNEL_COUNT];
    uint32_t scan_count = 0;

    NRF_POWER->DCDCEN = 1;

    hal_timer_init();
    hal_radio_init();
    rtc_init();
    uart_init();

    const uint32_t interval_ticks = rtc_ms_to_ticks(SCAN_INTERVAL_MS);

    while (1) {
        uint32_t duration_us = 0;
        uint32_t hfclk_us = 0;
        hal_radio_scan_timing_t timing;

        scan_count++;
        run_one_scan(rssi_dbm, &duration_us, &hfclk_us, &timing);
        scan_report_to_json(rssi_dbm, duration_us, hfclk_us, &timing, scan_count);

        rtc_schedule_compare_after(interval_ticks);
        rtc_wait_compare();
    }

    return 0;
}
