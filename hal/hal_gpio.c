/*
 * hal_gpio.c - GPIO driver for RH850/F1KM-S1
 *
 * Supports ports 0, 9, 10 (used on 983HH board).
 * Extend the switch statements to add more ports.
 */

#include "hal_gpio.h"
#include "board.h"

void hal_gpio_set_output(uint8 port, uint8 bit)
{
    uint32 clr = PSR_CLR(bit);

    switch (port)
    {
    case 0:
        PORTPMCSR0 = clr;      /* GPIO mode */
        PORTPMSR0  = clr;      /* Output */
        break;
    case 9:
        PORTPMCSR9 = clr;
        PORTPMSR9  = clr;
        break;
    case 10:
        PORTPMCSR10 = clr;
        PORTPMSR10  = clr;
        break;
    default:
        break;
    }
}

void hal_gpio_write(uint8 port, uint8 bit, uint8 value)
{
    uint32 cmd = value ? PSR_SET(bit) : PSR_CLR(bit);

    switch (port)
    {
    case 0:  PORTPSR0  = cmd; break;
    case 9:  PORTPSR9  = cmd; break;
    case 10: PORTPSR10 = cmd; break;
    default: break;
    }
}

void hal_gpio_set_analog_input(uint8 start_bit, uint8 count)
{
    uint8  i;
    uint16 mask = 0u;

    /* Set each analog port pin as input */
    for (i = 0u; i < count; i++)
    {
        PORTAPMSR0 = PSR_SET(start_bit + i);
        mask |= (uint16)(1u << (start_bit + i));
    }

    /* Enable analog port input buffer for digital readback */
    PORTAPIBC0 |= mask;
}

uint8 hal_gpio_read_dip(void)
{
    return (uint8)((PORTAPPR0 >> BOARD_DIP_START_BIT) &
                   ((1u << BOARD_DIP_COUNT) - 1u));
}
