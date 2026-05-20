/*********************************************************************
 *  SoC-specific LFCLK, interval sleep, DCDC, and HFCLK for RADIO.
 ********************************************************************/

#include "hal_platform.h"
#include "hal_soc.h"
#include <mdk/nrf.h>
#include <stdbool.h>
#include <zephyr/irq.h>

#if NRFSCAN_SOC_NRF54
#include <nrfx_clock.h>
#include <zephyr/drivers/clock_control/nrf_clock_control.h>
#include <zephyr/kernel.h>
#else

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

#endif

void hal_dcdc_enable(void)
{
#if NRFSCAN_SOC_NRF54
    NRF_REGULATORS->VREGMAIN.DCDCEN =
        REGULATORS_VREGMAIN_DCDCEN_VAL_Enabled << REGULATORS_VREGMAIN_DCDCEN_VAL_Pos;
#else
    NRF_POWER->DCDCEN = 1;
#endif
}

void hal_clock_hfclk_start(void)
{
#if NRFSCAN_SOC_NRF54
    /*
     * Same path as BLE Link Layer (lll_hfclock_on / lll_hfclock_on_wait):
     * request HFCLK for radio use and wait until HFXO is running.
     */
    z_nrf_clock_bt_ctlr_hf_request();

    for (uint32_t spin = 0; spin < 500000U; spin++) {
        nrf_clock_hfclk_t src;

        if (nrfx_clock_is_running(NRF_CLOCK_DOMAIN_HFCLK, &src) &&
            (src == NRF_CLOCK_HFCLK_HIGH_ACCURACY)) {
            return;
        }
    }
#else
    NRF_CLOCK->EVENTS_HFCLKSTARTED = 0;
    NRF_CLOCK->TASKS_HFCLKSTART = 1;
    while (NRF_CLOCK->EVENTS_HFCLKSTARTED == 0) {
    }
    NRF_CLOCK->EVENTS_HFCLKSTARTED = 0;
#endif
}

void hal_clock_hfclk_stop(void)
{
#if NRFSCAN_SOC_NRF54
    /* Keep HFCLK up; Zephyr/GRTC and the next scan need it. */
#else
    NRF_CLOCK->TASKS_HFCLKSTOP = 1;
#endif
}

void hal_lfclk_start(void)
{
#if !NRFSCAN_SOC_NRF54
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
#endif
}

void hal_interval_init(void)
{
    hal_lfclk_start();

#if !NRFSCAN_SOC_NRF54
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
#endif
}

#if !NRFSCAN_SOC_NRF54

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

#endif

void hal_interval_wait_ms(uint32_t ms)
{
#if NRFSCAN_SOC_NRF54
    k_msleep(ms);
#else
    const uint32_t ticks = rtc_ms_to_ticks(ms);

    rtc_schedule_compare_after(ticks);
    while (!rtc_compare_wake) {
        cpu_wfe();
    }
    rtc_compare_wake = false;
#endif
}
