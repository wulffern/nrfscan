/*********************************************************************
 *  Bare-metal LFCLK, HFXO/HFCLK, interval sleep (RTC2 or GRTC + WFE).
 *
 *  Built as a Zephyr app: IRQ_CONNECT wires compare ISRs into the vector
 *  table.  On nRF54 one GRTC compare (z_nrf_grtc_timer) wakes us; with
 *  CONFIG_TICKLESS_KERNEL Zephyr does not schedule periodic sys ticks.
 ********************************************************************/

#include "hal_platform.h"
#include "hal_soc.h"
#include <mdk/nrf.h>
#include <stdbool.h>
#include <zephyr/irq.h>

#if NRFSCAN_SOC_NRF54
#include <zephyr/drivers/timer/nrf_grtc_timer.h>
#include <zephyr/kernel.h>
#endif

#define RTC_TICKS_PER_SEC 32768U
#define RTC_COUNTER_MASK  0xFFFFFFU
#define RTC_CC_CHANNEL    0

#define CLOCK_SPIN_LIMIT  5000000U

static volatile bool interval_wake;

static inline void cpu_wfe(void)
{
    __WFE();
    __SEV();
    __WFE();
}

#if NRFSCAN_SOC_NRF54

static bool clock_xo_running(void)
{
    return (NRF_CLOCK->XO.STAT & CLOCK_XO_STAT_STATE_Msk) ==
           (CLOCK_XO_STAT_STATE_Running << CLOCK_XO_STAT_STATE_Pos);
}

static bool lfclk_ready;

static int32_t grtc_interval_chan = -1;

static void grtc_interval_compare_isr(int32_t chan, uint64_t expire_time, void *user_data)
{
    ARG_UNUSED(chan);
    ARG_UNUSED(expire_time);
    ARG_UNUSED(user_data);

    interval_wake = true;
}

#else

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

    interval_wake = false;
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
    interval_wake = true;
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
    uint32_t spin;

    if (clock_xo_running()) {
        return;
    }

    NRF_CLOCK->EVENTS_XOSTARTED = 0;
    NRF_CLOCK->TASKS_XOSTART = CLOCK_TASKS_XOSTART_TASKS_XOSTART_Trigger;
    for (spin = 0; spin < CLOCK_SPIN_LIMIT && NRF_CLOCK->EVENTS_XOSTARTED == 0; spin++) {
    }
    NRF_CLOCK->EVENTS_XOSTARTED = 0;
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
    uint32_t spin;

    if (!clock_xo_running()) {
        return;
    }

    NRF_CLOCK->TASKS_XOSTOP = CLOCK_TASKS_XOSTOP_TASKS_XOSTOP_Trigger;
    for (spin = 0; spin < CLOCK_SPIN_LIMIT && clock_xo_running(); spin++) {
    }
#else
    NRF_CLOCK->TASKS_HFCLKSTOP = 1;
#endif
}

void hal_lfclk_start(void)
{
#if NRFSCAN_SOC_NRF54
    if (lfclk_ready) {
        return;
    }

    NRF_CLOCK->LFCLK.SRC =
        (CLOCK_LFCLK_SRC_SRC_LFRC << CLOCK_LFCLK_SRC_SRC_Pos) & CLOCK_LFCLK_SRC_SRC_Msk;
    NRF_CLOCK->LFCLK.SRCCOPY = NRF_CLOCK->LFCLK.SRC;
    NRF_CLOCK->EVENTS_LFCLKSTARTED = 0;
    NRF_CLOCK->TASKS_LFCLKSTART = CLOCK_TASKS_LFCLKSTART_TASKS_LFCLKSTART_Trigger;
    while (NRF_CLOCK->EVENTS_LFCLKSTARTED == 0) {
    }
    NRF_CLOCK->EVENTS_LFCLKSTARTED = 0;
    lfclk_ready = true;
#else
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

#if NRFSCAN_SOC_NRF54
    if (grtc_interval_chan < 0) {
        grtc_interval_chan = z_nrf_grtc_timer_chan_alloc();
    }
#else
    NRF_RTC2->TASKS_STOP = 1;
    NRF_RTC2->TASKS_CLEAR = 1;
    NRF_RTC2->PRESCALER = 0;
    NRF_RTC2->EVTENCLR = 0xFFFFFFFF;
    NRF_RTC2->INTENCLR = 0xFFFFFFFF;
    NRF_RTC2->EVENTS_COMPARE[RTC_CC_CHANNEL] = 0;
    NRF_RTC2->TASKS_START = 1;

    interval_wake = false;
    IRQ_CONNECT(RTC2_IRQn, 6, rtc2_compare_isr, NULL, 0);
    irq_enable(RTC2_IRQn);
#endif
}

void hal_interval_wait_ms(uint32_t ms)
{
#if NRFSCAN_SOC_NRF54
    const uint64_t target = z_nrf_grtc_timer_get_ticks(K_MSEC(ms));

    if (grtc_interval_chan < 0) {
        return;
    }

    interval_wake = false;
    z_nrf_grtc_timer_abort(grtc_interval_chan);
    if (z_nrf_grtc_timer_set(grtc_interval_chan, target, grtc_interval_compare_isr, NULL) != 0) {
        return;
    }

    while (!interval_wake) {
        cpu_wfe();
    }

    z_nrf_grtc_timer_abort(grtc_interval_chan);
#else
    const uint32_t ticks = rtc_ms_to_ticks(ms);

    rtc_schedule_compare_after(ticks);
    while (!interval_wake) {
        cpu_wfe();
    }
    interval_wake = false;
#endif
}
