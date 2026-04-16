/*
 * board_init.c - 983HH board bring-up derived from bios-ver-11.txt
 *
 * This covers the shared cold-boot port and power sequence:
 *   - port direction / initial level setup
 *   - AP0_5/AP0_6 power enables
 *   - P9_6 PDB assertion
 *   - DIP input buffer enable
 *
 * The 2 s startup delay and I2C bus init stay app-local in 983_manager.
 */

#include "board.h"
#include "hal_gpio.h"

/* ---- Pre-PLL delay helper ---- */

static void delay_ms_prepll(uint32 ms)
{
    uint32 outer;

    /*
     * board_init() is called before PLL switch in 983_manager.
     * The default CPU clock is slow enough that a conservative busy-wait
     * is acceptable here; overshoot is harmless for this power sequence.
     */
    for (outer = 0u; outer < ms; outer++)
    {
        volatile uint32 d = 1500u;
        while (d-- != 0u)
        {
            ;
        }
    }
}

/* ---- AP0 helpers ---- */

static void ap0_set_direction(uint8 bit, uint8 is_input)
{
    if (is_input != 0u)
    {
        PORTAPMSR0 = PSR_SET(bit);
    }
    else
    {
        PORTAPMSR0 = PSR_CLR(bit);
    }
}

static void ap0_write(uint8 bit, uint8 value)
{
    if (value != 0u)
    {
        PORTAPSR0 = PSR_SET(bit);
    }
    else
    {
        PORTAPSR0 = PSR_CLR(bit);
    }
}

static void ap0_drive_high(uint8 bit)
{
    ap0_write(bit, 1u);
    ap0_set_direction(bit, 0u);
}

/* ---- Shared digital port helpers ---- */

static void port_apply_levels(volatile uint32 *psr_reg, uint16 clear_mask, uint16 set_mask)
{
    uint8 bit;

    for (bit = 0u; bit < 16u; bit++)
    {
        if ((clear_mask & ((uint16)1u << bit)) != 0u)
        {
            *psr_reg = PSR_CLR(bit);
        }
        if ((set_mask & ((uint16)1u << bit)) != 0u)
        {
            *psr_reg = PSR_SET(bit);
        }
    }
}

static void port_apply_outputs(volatile uint32 *pmcsr_reg,
                               volatile uint32 *pmsr_reg,
                               uint16 output_mask)
{
    uint8 bit;

    for (bit = 0u; bit < 16u; bit++)
    {
        if ((output_mask & ((uint16)1u << bit)) != 0u)
        {
            *pmcsr_reg = PSR_CLR(bit);
            *pmsr_reg  = PSR_CLR(bit);
        }
    }
}

static void jtag_port_set_all_inputs(void)
{
    uint8 bit;

    for (bit = 0u; bit < 8u; bit++)
    {
        JTAGJPMSR0 = PSR_SET(bit);
    }
}

/* ---- BIOS-derived port configuration ---- */

static void port_init(void)
{
    uint8 bit;

    /* Port AP0: outputs on AP0_5/AP0_6, other pins stay inputs. */
    ap0_write(PIN_IOC_ON_UG1V8_BIT, 0u);
    ap0_write(PIN_IOC_ON_UG1V15_BIT, 0u);
    for (bit = 0u; bit < 16u; bit++)
    {
        ap0_set_direction(bit, ((bit == PIN_IOC_ON_UG1V8_BIT) ||
                                (bit == PIN_IOC_ON_UG1V15_BIT)) ? 0u : 1u);
    }

    /* JP0: all inputs with Schmitt input buffer enabled. */
    jtag_port_set_all_inputs();
    JTAGJPIBC0 |= 0xFFu;

    /* Port P0: apply the BIOS script exactly during strap sampling. */
    port_apply_levels(&PORTPSR0, 0x1161u, 0x4800u);
    port_apply_outputs(&PORTPMCSR0, &PORTPMSR0, 0x5961u);
    PORTPIBC0 |= 0x269Eu;

    /* Port P8 */
    port_apply_levels(&PORTPSR8, 0x1C23u, 0x0200u);
    port_apply_outputs(&PORTPMCSR8, &PORTPMSR8, 0x1E23u);
    PORTPIBC8 |= 0x01DCu;

    /* Port P9 */
    port_apply_levels(&PORTPSR9, 0x0001u, 0x006Eu);
    port_apply_outputs(&PORTPMCSR9, &PORTPMSR9, 0x006Fu);
    PORTPIBC9 |= 0xFF90u;

    /* Port P10 */
    port_apply_levels(&PORTPSR10, 0x14BFu, 0x0000u);
    port_apply_outputs(&PORTPMCSR10, &PORTPMSR10, 0x34BFu);
    PORTPIBC10 |= 0xCB40u;
}

void board_init(void)
{
    port_init();
    delay_ms_prepll(1u);

    /* Main power rails */
    ap0_drive_high(PIN_IOC_ON_UG1V8_BIT);
    ap0_drive_high(PIN_IOC_ON_UG1V15_BIT);

    delay_ms_prepll(20u);

    /* Serializer PDB */
    hal_gpio_write(PIN_PDB_PORT, PIN_PDB_BIT, 1u);
    hal_gpio_set_output(PIN_PDB_PORT, PIN_PDB_BIT);

    /* DIP switches are active-low inputs on AP0_7..14. */
    hal_gpio_set_analog_input(BOARD_DIP_START_BIT, BOARD_DIP_COUNT);
}
