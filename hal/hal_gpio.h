/*
 * hal_gpio.h - GPIO driver
 */

#ifndef HAL_GPIO_H
#define HAL_GPIO_H

#include "dr7f701686.dvf.h"

/*
 * Atomic set/reset pattern for PSR/PMSR/PMCSR registers.
 * Upper 16 bits = mask (which bits to modify)
 * Lower 16 bits = value (1=set, 0=clear)
 */
#define PSR_SET(bit)    ((uint32)0x00010001u << (bit))
#define PSR_CLR(bit)    ((uint32)0x00010000u << (bit))

/* Set a digital port pin as GPIO output */
void hal_gpio_set_output(uint8 port, uint8 bit);

/* Write a digital port pin (1=high, 0=low) */
void hal_gpio_write(uint8 port, uint8 bit, uint8 value);

/* Set analog port pins as digital inputs (AP0 only, enables input buffer) */
void hal_gpio_set_analog_input(uint8 start_bit, uint8 count);

/* Read DIP switches from AP0 (uses BOARD_DIP_START_BIT / BOARD_DIP_COUNT) */
uint8 hal_gpio_read_dip(void);

#endif /* HAL_GPIO_H */
