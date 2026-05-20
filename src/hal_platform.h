#ifndef HAL_PLATFORM_H__
#define HAL_PLATFORM_H__

#include <stdint.h>

void hal_dcdc_enable(void);
void hal_clock_hfclk_start(void);
void hal_clock_hfclk_stop(void);
void hal_lfclk_start(void);
void hal_interval_init(void);
void hal_interval_wait_ms(uint32_t ms);

#endif
