/*
 * hal_clock.h - Clock system and protected write helpers
 */

#ifndef HAL_CLOCK_H
#define HAL_CLOCK_H

#include "dr7f701686.dvf.h"

/* Protected write to PROTCMD0-guarded registers (AWO domain) */
void hal_prot_write0(volatile uint32 *reg, uint32 value);

/* Protected write to PROTCMD1-guarded registers (ISO domain) */
void hal_prot_write1(volatile uint32 *reg, uint32 value);

/*
 * Initialize PLL1 and all clock domains.
 *
 * After CPU clock switches to PLL, the LIN clock changes immediately
 * (CPUCLK2: 4 MHz -> 40 MHz). If UART is active, it must be reinited
 * at the new clock rate to avoid garbled output.
 *
 * post_pll_cb: called right after CPU clock switch (use for UART
 *              reinit), or NULL if not needed.
 */
void hal_clock_init(void (*post_pll_cb)(void));

#endif /* HAL_CLOCK_H */
