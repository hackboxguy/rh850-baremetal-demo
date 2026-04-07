/*
 * blink_led - Simple LED blink demo
 *
 * Blinks the on-board LED (P9_6) with ~500 ms period.
 * No PLL - runs on default 8 MHz HS IntOSC.
 *
 * Build:
 *   make BOARD=983HH APP=blink_led
 *   make BOARD=983HH APP=blink_led DEBUG=on    (enables UART boot banner)
 */

#include "board.h"
#include "hal_gpio.h"
#include "lib_boot.h"

static void delay(uint32 cnt)
{
    volatile uint32 d = cnt;
    while (d-- != 0u)
    {
        ;
    }
}

int main(void)
{
    BOOT_BANNER("blink_led");

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
