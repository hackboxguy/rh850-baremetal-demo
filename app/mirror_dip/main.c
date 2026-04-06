/*
 * mirror_dip - Mirror DIP switch 1 to LED
 *
 * Reads DIP switch AP0_7 and mirrors its state to LED P9_6.
 * No PLL, no UART - runs on default 8 MHz HS IntOSC.
 *
 * Build: make BOARD=983HH APP=mirror_dip
 */

#include "board.h"
#include "hal_gpio.h"

int main(void)
{
    board_init();

    for (;;)
    {
        uint8 dip = hal_gpio_read_dip();
        hal_gpio_write(BOARD_LED_PORT, BOARD_LED_BIT, dip & 0x01u);
    }

    return 0;
}
