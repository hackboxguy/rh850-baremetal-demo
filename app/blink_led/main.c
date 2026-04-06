/*
 * blink_led - Simple LED blink demo
 *
 * Blinks the on-board LED (P9_6) with ~500 ms period.
 * No PLL, no UART - runs on default 8 MHz HS IntOSC.
 *
 * Build: make BOARD=983HH APP=blink_led
 */

#include "board.h"
#include "hal_gpio.h"

static void delay(volatile uint32 count)
{
    while (count--)
        ;
}

int main(void)
{
    hal_gpio_set_output(BOARD_LED_PORT, BOARD_LED_BIT);

    for (;;)
    {
        hal_gpio_write(BOARD_LED_PORT, BOARD_LED_BIT, 1);
        delay(500000);

        hal_gpio_write(BOARD_LED_PORT, BOARD_LED_BIT, 0);
        delay(500000);
    }

    return 0;
}
