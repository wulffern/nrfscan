#ifndef HAL_SOC_H__
#define HAL_SOC_H__

#ifndef NRFSCAN_SOC_NRF54
#define NRFSCAN_SOC_NRF54 0
#endif

#if NRFSCAN_SOC_NRF54
/* DK USB VCOM is wired to uart20 / NRF_UARTE20 on P1.04 (TX) and P1.05 (RX). */
#define HAL_UARTE_INST NRF_UARTE20
#define HAL_TIMER      NRF_TIMER21
#ifndef TXD_PIN
#define TXD_PIN 36 /* NRF_PIN_PORT_TO_PIN_NUMBER(4, 1) */
#endif
#ifndef RXD_PIN
#define RXD_PIN 37 /* NRF_PIN_PORT_TO_PIN_NUMBER(5, 1) */
#endif
#else
#define HAL_UARTE_INST NRF_UARTE0
#define HAL_TIMER      NRF_TIMER1
#ifndef TXD_PIN
#define TXD_PIN 6
#endif
#ifndef RXD_PIN
#define RXD_PIN 8
#endif
#endif

#endif
