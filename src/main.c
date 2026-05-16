/*********************************************************************
 *  nrfscan: sweep RSSI on all BLE channels, JSON on UART every 1 s.
 *********************************************************************/

#include "hal_radio.h"
#include <mdk/nrf.h>
#include <nrfx_uarte.h>
#include <stdbool.h>
#include <stdint.h>

#ifndef TXD_PIN
#define TXD_PIN 6
#endif

#ifndef RXD_PIN
#define RXD_PIN 8
#endif

#ifndef SCAN_DWELL_US
#define SCAN_DWELL_US 1
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

static void scan_report_to_json(const int8_t *rssi_dbm, uint32_t dwell_us, uint32_t scan_duration_us,
                                uint32_t hfclk_us, const hal_radio_scan_timing_t *timing,
                                uint32_t scan_count)
{
    const uint32_t sweep_accounted = timing->disable_us + timing->ready_us + timing->settle_us +
                                     timing->rssi_us + timing->final_disable_us;

    uart_put_string("{");
    int8_array_to_json("rssi_dB", rssi_dbm, RSSI_CHANNEL_COUNT);
    uart_put_string(",");
    int_to_json("dwell_us", (int)dwell_us);
    uart_put_char(',');
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
    NRF_RTC2->TASKS_START = 1;
}

static uint32_t rtc_ticks(void)
{
    return NRF_RTC2->COUNTER;
}

static uint32_t rtc_ms_to_ticks(uint32_t ms)
{
    return (RTC_TICKS_PER_SEC * ms) / 1000U;
}

static void rtc_wait_until(uint32_t target)
{
    while ((int32_t)(rtc_ticks() - target) < 0) {
        __WFE();
    }
}

static void run_one_scan(int8_t *rssi_dbm, uint32_t *duration_us, uint32_t *hfclk_us,
                         hal_radio_scan_timing_t *timing)
{
    const uint32_t hfclk_start = hal_time_us();
    hal_clock_hfclk_start();
    *hfclk_us = hal_time_us() - hfclk_start;

    const uint32_t scan_start = hal_time_us();
    hal_radio_scan_rssi(rssi_dbm, SCAN_DWELL_US, timing);
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
        const uint32_t period_start = rtc_ticks();
        uint32_t duration_us = 0;
        uint32_t hfclk_us = 0;
        hal_radio_scan_timing_t timing;

        scan_count++;
        run_one_scan(rssi_dbm, &duration_us, &hfclk_us, &timing);
        scan_report_to_json(rssi_dbm, SCAN_DWELL_US, duration_us, hfclk_us, &timing, scan_count);

        const uint32_t target = period_start + interval_ticks;
        rtc_wait_until(target);
    }

    return 0;
}
