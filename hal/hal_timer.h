/*
 * hal_timer.h - OSTM0 interval timer driver
 *
 * Provides a periodic interrupt for background tasks
 * (e.g. draining the debug ring buffer to UART).
 */

#ifndef HAL_TIMER_H
#define HAL_TIMER_H

#include "dr7f701686.dvf.h"

/*
 * Initialize OSTM0 as interval timer and start it.
 * Configures INTC2 interrupt (ch84, table reference).
 *
 * interval_us: timer period in microseconds.
 * pclk_hz:     peripheral clock feeding OSTM0 (typically CPUCLK2 = 40 MHz).
 * callback:    function called from the timer ISR each interval.
 *
 * Requires PLL running and global interrupts enabled (__EI).
 */
void hal_timer_init(uint32 interval_us, uint32 pclk_hz,
                    void (*callback)(void));

/* Stop OSTM0 timer */
void hal_timer_stop(void);

/* ISR handler (called from EIINTTBL in boot.asm, do not call directly) */
void hal_ostm0_isr(void);

#endif /* HAL_TIMER_H */
