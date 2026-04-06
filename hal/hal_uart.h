/*
 * hal_uart.h - RLIN3 UART driver (polling TX)
 */

#ifndef HAL_UART_H
#define HAL_UART_H

#include "dr7f701686.dvf.h"

/*
 * Initialize UART (RLIN32) with given peripheral clock and baud rate.
 * Configures pins (from board.h) and baud rate generator.
 *
 * Common usage:
 *   hal_uart_init(4000000, 115200)   -- before PLL (HS IntOSC/2)
 *   hal_uart_init(40000000, 115200)  -- after PLL (PPLLCLK2)
 */
void hal_uart_init(uint32 pclk_hz, uint32 baud);

/* Transmit a single byte (blocking, waits for TX idle) */
void hal_uart_putc(uint8 c);

/* Transmit a null-terminated string (\n automatically sends \r\n) */
void hal_uart_puts(const char *s);

/* Print a byte as 2-digit hex (e.g. 0xAB -> "AB") */
void hal_uart_put_hex8(uint8 val);

/* Print a 32-bit value as 8-digit hex */
void hal_uart_put_hex32(uint32 val);

#endif /* HAL_UART_H */
