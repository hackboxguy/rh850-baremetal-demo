/*
 * lib_boot.h - Standard boot banner (conditional on DEBUG_ENABLED)
 *
 * When DEBUG_ENABLED is defined (make DEBUG=on), BOOT_BANNER() initializes
 * the UART at the pre-PLL clock rate and prints a standard boot message:
 *
 *     === <app_name> ===
 *     Board: 983HH | FW: vMM.mm
 *     Ready.
 *
 * When DEBUG is off, BOOT_BANNER() compiles to nothing (zero overhead).
 *
 * Usage: call BOOT_BANNER("app_name") at the top of main(), before
 *        any other initialization.
 *
 * For apps with PLL: after BOOT_BANNER(), call hal_clock_init() with
 * boot_uart_reinit as the callback to reinit UART at the PLL clock rate.
 * This callback is also conditional — returns NULL when DEBUG is off.
 */

#ifndef LIB_BOOT_H
#define LIB_BOOT_H

#include "board.h"
#include "hal_uart.h"

#ifdef DEBUG_ENABLED

/* Print standard boot banner (blocking UART at pre-PLL clock) */
void boot_print_banner(const char *app_name);

/* Reinit UART at PLL clock rate (for use as hal_clock_init callback) */
void boot_uart_reinit(void);

#define BOOT_BANNER(name)       boot_print_banner(name)
#define BOOT_UART_REINIT_CB     boot_uart_reinit

#else

#define BOOT_BANNER(name)       ((void)0)
#define BOOT_UART_REINIT_CB     ((void (*)(void))0)

#endif /* DEBUG_ENABLED */

#endif /* LIB_BOOT_H */
