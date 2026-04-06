/*
 * hal_uart.h - RLIN3 UART driver (blocking + non-blocking TX)
 *
 * Blocking API (hal_uart_putc/puts/...): waits for TX idle, used for
 * boot messages before the timer is running.
 *
 * Non-blocking API (hal_uart_nb_putc/puts/...): pushes bytes into a
 * ring buffer (~1 us per call). A timer ISR calls hal_uart_drain()
 * to transmit queued bytes. Use for debug prints inside ISRs.
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

/* ---- Blocking API (for boot/init messages) ---- */

/* Transmit a single byte (blocking, waits for TX idle) */
void hal_uart_putc(uint8 c);

/* Transmit a null-terminated string (\n automatically sends \r\n) */
void hal_uart_puts(const char *s);

/* Print a byte as 2-digit hex (e.g. 0xAB -> "AB") */
void hal_uart_put_hex8(uint8 val);

/* Print a 32-bit value as 8-digit hex */
void hal_uart_put_hex32(uint32 val);

/* ---- Non-blocking API (for ISR debug prints) ---- */

/* Initialize the non-blocking TX ring buffer.
 * Call once after hal_uart_init(), before using nb_ functions. */
void hal_uart_nb_init(void);

/* Push a byte to the ring buffer (non-blocking, ~1 us).
 * Silently drops if buffer is full. */
void hal_uart_nb_putc(uint8 c);

/* Push a string to the ring buffer (non-blocking). */
void hal_uart_nb_puts(const char *s);

/* Push a hex byte to the ring buffer (non-blocking). */
void hal_uart_nb_put_hex8(uint8 val);

/*
 * Drain up to max_bytes from the ring buffer to UART hardware.
 * Call from a periodic timer ISR (e.g. every 1 ms).
 * Transmits only if UART TX is idle (non-blocking per byte).
 * Returns number of bytes actually transmitted.
 */
uint32 hal_uart_drain(uint32 max_bytes);

#endif /* HAL_UART_H */
