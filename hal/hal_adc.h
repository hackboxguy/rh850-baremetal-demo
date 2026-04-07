/*
 * hal_adc.h - ADCA0 analog-to-digital converter driver (polling)
 *
 * Single-channel software-triggered ADC using scan group 1.
 * 12-bit resolution, right-aligned result.
 */

#ifndef HAL_ADC_H
#define HAL_ADC_H

#include "dr7f701686.dvf.h"

/* Signed types (dvf.h only defines unsigned) */
typedef signed short    int16;
typedef signed long     int32;

/*
 * Initialize ADCA0 for single-channel polling conversion.
 * channel: physical channel number (0 = ANI00 = AP0_0)
 *
 * Requires PLL running (ADCA0 clocked from peripheral bus).
 */
void hal_adc_init(uint8 channel);

/*
 * Trigger a single conversion and return the 12-bit result (0-4095).
 * Blocks until conversion completes (~2 us at 40 MHz PCLK).
 */
uint16 hal_adc_read(void);

#endif /* HAL_ADC_H */
