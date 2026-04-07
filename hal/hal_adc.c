/*
 * hal_adc.c - ADCA0 driver for RH850/F1KM-S1 (polling mode)
 *
 * Register values derived from Renesas Smart Configurator output
 * (Config_ADCA0.c for R7F701686).
 *
 * Configuration:
 *   - Scan group 1, single virtual channel (VCR00)
 *   - 12-bit resolution, right-aligned
 *   - 18 sampling cycles
 *   - Software trigger, multicycle scan mode
 *   - No T&H, no interrupts (polling only)
 *
 * ADCA0 base: 0xFFF20000
 * Result register base: 0xFFF20100 (DIR00-DIR35, 32-bit pairs)
 */

#include "hal_adc.h"

/* ADC result data register for virtual channel 0 (16-bit result in low half) */
#define ADCA0_DIR00     (*(volatile uint32 *)0xFFF20100u)

void hal_adc_init(uint8 channel)
{
    /* Halt ADC if running */
    ADCA0.ADHALTR.UINT32 = 0x00000001u;

    /* Virtual channel 00: map to physical channel, no MPX, no limit check */
    ADCA0.VCR00.UINT32 = (uint32)channel;

    /* Operation: synchronous suspend, 12-bit, right-aligned */
    ADCA0.ADCR.UINT32 = 0x00000000u;

    /* Sampling time: 18 cycles */
    ADCA0.SMPCR.UINT32 = 0x00000012u;

    /* Error check: disabled (no overwrite/limit interrupts for polling) */
    ADCA0.SFTCR.UINT32 = 0x00000000u;

    /* Self-diagnosis: disabled */
    ADCA0.DGCTL0.UINT32 = 0x00000000u;

    /* T&H: disabled */
    ADCA0.THER.UINT32 = 0x00000000u;

    /* Scan group 1: multicycle mode, 1 scan, no HW trigger */
    ADCA0.SGCR1.UINT32 = 0x00000000u;
    ADCA0.SGVCSP1.UINT32 = 0x00000000u;    /* Start: virtual channel 0 */
    ADCA0.SGVCEP1.UINT32 = 0x00000000u;    /* End: virtual channel 0 */
    ADCA0.SGMCYCR1.UINT32 = 0x00000000u;   /* 1 scan cycle */

    /* Synchronization barrier */
    {
        volatile uint32 sync = ADCA0.ADHALTR.UINT32;
        (void)sync;
    }
    __syncp();
}

uint16 hal_adc_read(void)
{
    volatile uint32 t = 0x10000u;

    /* Trigger scan group 1 */
    ADCA0.SGSTCR1.UINT32 = 0x00000001u;

    /* Wait for SG1 to complete (SGACT1 bit in SGSTR clears) */
    while (t != 0u)
    {
        if ((ADCA0.SGSTR.UINT32 & 0x00010000u) == 0u)
        {
            break;
        }
        t--;
    }

    /* Read 12-bit result from DIR00 (bits 11:0) */
    return (uint16)(ADCA0_DIR00 & 0x0FFFu);
}
