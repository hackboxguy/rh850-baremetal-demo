/*
 * lib_boot.c - Standard boot banner implementation
 *
 * Only compiled into the binary when DEBUG_ENABLED is defined.
 * The functions are guarded so the object file is empty in release builds.
 */

#include "lib_boot.h"

#ifdef DEBUG_ENABLED

void boot_print_banner(const char *app_name)
{
    /* Init UART at pre-PLL clock (4 MHz on 983HH) */
    hal_uart_init(BOARD_PCLK_NOPLL_HZ, BOARD_UART_BAUD);

    hal_uart_puts("\n=== ");
    hal_uart_puts(app_name);
    hal_uart_puts(" ===\nBoard: " BOARD_NAME " | FW: v");
    hal_uart_put_hex8((uint8)FW_VERSION_MAJOR);
    hal_uart_putc('.');
    hal_uart_put_hex8((uint8)FW_VERSION_MINOR);
    hal_uart_puts(" | Built: ");
    hal_uart_puts(__DATE__);
    hal_uart_putc(' ');
    hal_uart_puts(__TIME__);
    hal_uart_puts("\nReady.\n");
}

void boot_uart_reinit(void)
{
    hal_uart_init(BOARD_PCLK_HZ, BOARD_UART_BAUD);
}

#endif /* DEBUG_ENABLED */
