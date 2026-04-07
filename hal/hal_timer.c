/*
 * hal_timer.c - OSTM0 interval timer for RH850/F1KM-S1
 *
 * OSTM0 is a simple 32-bit down-counter at 0xFFD70000.
 * Clock source: CPUCLK2 (40 MHz with PLL on 983HH).
 *
 * In interval mode (CTL bit0=0, bit1=0):
 *   - Counter loads CMP value on start
 *   - Counts down to 0, fires interrupt, auto-reloads CMP
 *   - Period = (CMP + 1) / clock_hz
 *
 * Interrupt channel 84, INTC2 register at 0xFFFFB0A8.
 */

#include "hal_timer.h"
#include "board_vectors.h"

/* INTC2 control register for OSTM0 */
#define ICOSTM0     (*(volatile uint16 *)ICR_OSTM0_ADDR)

/* INTC2 bit definitions */
#define ICR_MK      ((uint16)1u << 7)       /* Interrupt mask */
#define ICR_TB      ((uint16)1u << 6)       /* Table reference */
#define ICR_RF      ((uint16)1u << 12)      /* Request flag */

static void (*g_timer_cb)(void);

void hal_timer_init(uint32 interval_us, uint32 pclk_hz,
                    void (*callback)(void))
{
    uint32 cmp_val;

    g_timer_cb = callback;

    /* Calculate compare value: CMP = (pclk * interval_us / 1000000) - 1 */
    cmp_val = ((pclk_hz / 1000000u) * interval_us) - 1u;

    /* Stop timer if running */
    OSTM0TT = 0x01u;

    /* Mask interrupt, clear flag, set table reference */
    ICOSTM0 |= ICR_MK;
    ICOSTM0 &= ~ICR_RF;
    ICOSTM0 |= ICR_TB;

    /* Configure OSTM0 */
    OSTM0CTL = 0x00u;          /* Interval mode, interrupt at start of count */
    OSTM0CMP = cmp_val;        /* Set period */

    /* Clear flag, unmask interrupt */
    ICOSTM0 &= ~ICR_RF;
    ICOSTM0 &= ~ICR_MK;

    /* Start timer */
    OSTM0TS = 0x01u;
}

void hal_timer_stop(void)
{
    OSTM0TT = 0x01u;           /* Stop timer */
    ICOSTM0 |= ICR_MK;         /* Mask interrupt */
}

#pragma interrupt hal_ostm0_isr(enable=false, channel=84, fpu=true, callt=false)
void hal_ostm0_isr(void)
{
    if (g_timer_cb != (void *)0)
    {
        g_timer_cb();
    }
}
