/*
 * board_init.c - Board-specific initialization for 983HH
 */

#include "board.h"
#include "hal_gpio.h"

void board_init(void)
{
    /* Initialize LED as output, initially off */
    hal_gpio_set_output(BOARD_LED_PORT, BOARD_LED_BIT);
    hal_gpio_write(BOARD_LED_PORT, BOARD_LED_BIT, 0);

    /* Initialize DIP switch inputs (AP0_7 through AP0_14) */
    hal_gpio_set_analog_input(BOARD_DIP_START_BIT, BOARD_DIP_COUNT);
}
