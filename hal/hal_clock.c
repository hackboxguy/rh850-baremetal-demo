/*
 * hal_clock.c - PLL and clock domain initialization for RH850/F1KM-S1
 *
 * Sequence derived from working BIOS hw_init.c (verified on 983HH hardware).
 *
 * F1KM-S1 has PLL1 only (no PLL0).
 * MainOSC = 16 MHz -> PLL1 -> 480 MHz VCO -> PPLLCLK = 80 MHz
 *
 * After init:
 *   CPUCLK   = 80 MHz
 *   CPUCLK2  = 40 MHz
 *   PPLLCLK  = 80 MHz
 *   PPLLCLK2 = 40 MHz
 *   IPERI1   = 80 MHz (PPLLCLK)
 *   IPERI2   = 40 MHz (PPLLCLK2)
 *   LIN      = 40 MHz (PPLLCLK2)
 *   CSI      = 80 MHz (PPLLCLK)
 */

#include "hal_clock.h"
#include "board.h"

/* Clock registers not in dvf.h */
#define CKSC_ILINS_CTL  (*(volatile uint32 *)0xFFF8A800u)
#define CKSC_ILINS_ACT  (*(volatile const uint32 *)0xFFF8A808u)
#define CKSC_IIICS_CTL  (*(volatile uint32 *)0xFFF8AC00u)
#define CKSC_IIICS_ACT  (*(volatile const uint32 *)0xFFF8AC08u)

void hal_prot_write0(volatile uint32 *reg, uint32 value)
{
    WPROTRPROTCMD0 = 0xA5u;
    *reg =  value;
    *reg = ~value;
    *reg =  value;
}

void hal_prot_write1(volatile uint32 *reg, uint32 value)
{
    WPROTRPROTCMD1 = 0xA5u;
    *reg =  value;
    *reg = ~value;
    *reg =  value;
}

void hal_clock_init(void (*post_pll_cb)(void))
{
    uint32 value;

    /* 1. Wait for HS IntOSC to be active */
    while ((CLKCTLROSCS & 0x04u) == 0u)
        ;

    /* 2. Clear I/O Buffer Hold */
    hal_prot_write0(&STBC_IOHOLDIOHOLD, 0x00000000u);

    /* 3. Enable MainOSC (16 MHz) if not already running */
    if ((CLKCTLMOSCS & 0x04u) != 0x04u)
    {
        CLKCTLMOSCC  = 0x00000007u;    /* Gain setting for 8-16 MHz */
        CLKCTLMOSCST = 0x0001FFFFu;    /* Stabilization wait ~4 ms */

        value = 0x00000001u;
        WPROTRPROTCMD0 = 0xA5u;
        CLKCTLMOSCE =  value;
        CLKCTLMOSCE = ~value;
        CLKCTLMOSCE =  value;
    }
    while ((CLKCTLMOSCS & 0x04u) != 0x04u)
        ;

    /* 4. PLL1 input = MainOSC */
    value = 0x00000001u;
    hal_prot_write1(&CLKCTLCKSC_PLL1IS_CTL, value);
    while (CLKCTLCKSC_PLL1IS_ACT != value)
        ;

    /* 5. Configure PLL1: 16 MHz -> 480 MHz VCO -> 80 MHz PPLLCLK */
    CLKCTLPLL1C = BOARD_PLL1C_VALUE;

    /* 6. Enable PLL1 */
    value = 0x00000001u;
    hal_prot_write1(&CLKCTLPLL1E, value);
    while ((CLKCTLPLL1S & 0x04u) != 0x04u)
        ;

    /* 7. CPU clock divider = /1 */
    value = 0x00000001u;
    hal_prot_write1(&CLKCTLCKSC_CPUCLKD_CTL, value);
    while (CLKCTLCKSC_CPUCLKD_ACT != value)
        ;

    /* 8. CPU clock source = PLL1 output (80 MHz) */
    value = 0x00000003u;
    hal_prot_write1(&CLKCTLCKSC_CPUCLKS_CTL, value);
    while (CLKCTLCKSC_CPUCLKS_ACT != value)
        ;

    /*
     * CPU now runs at 80 MHz. LIN clock changed to CPUCLK2 = 40 MHz.
     * Call post_pll_cb (typically UART reinit) before further debug output.
     */
    if (post_pll_cb)
        post_pll_cb();

    /* 9. PPLLCLK source = PLL1 output */
    value = 0x00000003u;
    hal_prot_write1(&CLKCTLCKSC_PPLLCLKS_CTL, value);
    {
        volatile uint32 t = 0x10000u;
        while (CLKCTLCKSC_PPLLCLKS_ACT != value && --t)
            ;
    }

    /* 10. Peripheral clock domains */
    value = 0x00000002u;
    hal_prot_write1(&CLKCTLCKSC_IPERI1S_CTL, value);   /* PPLLCLK (80 MHz) */
    while (CLKCTLCKSC_IPERI1S_ACT != value)
        ;

    value = 0x00000002u;
    hal_prot_write1(&CLKCTLCKSC_IPERI2S_CTL, value);   /* PPLLCLK2 (40 MHz) */
    while (CLKCTLCKSC_IPERI2S_ACT != value)
        ;

    value = 0x00000003u;
    hal_prot_write1((volatile uint32 *)&CKSC_ILINS_CTL, value);  /* LIN: PPLLCLK2 */
    while (CKSC_ILINS_ACT != value)
        ;

    value = 0x00000002u;
    hal_prot_write1(&CLKCTLCKSC_ICSIS_CTL, value);     /* CSI: PPLLCLK */
    while (CLKCTLCKSC_ICSIS_ACT != value)
        ;
}
