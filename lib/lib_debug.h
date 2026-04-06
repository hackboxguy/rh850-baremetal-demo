/*
 * lib_debug.h - Debug output macros
 *
 * When DEBUG_ENABLED is defined (make DEBUG=on), these macros
 * output to the UART. Otherwise they compile to nothing.
 */

#ifndef LIB_DEBUG_H
#define LIB_DEBUG_H

#include "hal_uart.h"

#ifdef DEBUG_ENABLED
#define DBG_PUTC(c)     hal_uart_putc(c)
#define DBG_HEX8(v)     hal_uart_put_hex8(v)
#define DBG_HEX32(v)    hal_uart_put_hex32(v)
#define DBG_PUTS(s)     hal_uart_puts(s)
#else
#define DBG_PUTC(c)     ((void)0)
#define DBG_HEX8(v)     ((void)0)
#define DBG_HEX32(v)    ((void)0)
#define DBG_PUTS(s)     ((void)0)
#endif

#endif /* LIB_DEBUG_H */
