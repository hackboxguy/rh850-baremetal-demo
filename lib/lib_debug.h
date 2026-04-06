/*
 * lib_debug.h - Debug output macros
 *
 * When DEBUG_ENABLED is defined (make DEBUG=on), these macros output
 * to the UART via the non-blocking ring buffer path (~1 us per call).
 * A timer ISR drains the buffer to the actual UART hardware.
 *
 * Safe to call from ISRs (no blocking, no UART polling).
 * Otherwise they compile to nothing.
 */

#ifndef LIB_DEBUG_H
#define LIB_DEBUG_H

#include "hal_uart.h"

#ifdef DEBUG_ENABLED
#define DBG_PUTC(c)     hal_uart_nb_putc(c)
#define DBG_HEX8(v)     hal_uart_nb_put_hex8(v)
#define DBG_HEX32(v)    ((void)0)  /* not yet implemented in nb path */
#define DBG_PUTS(s)     hal_uart_nb_puts(s)
#else
#define DBG_PUTC(c)     ((void)0)
#define DBG_HEX8(v)     ((void)0)
#define DBG_HEX32(v)    ((void)0)
#define DBG_PUTS(s)     ((void)0)
#endif

#endif /* LIB_DEBUG_H */
