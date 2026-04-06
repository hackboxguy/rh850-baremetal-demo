/*
 * hal_uart.c - RLIN32 UART driver for RH850/F1KM-S1
 *
 * Blocking TX: polling on RLIN3 channel 2 (RLIN32).
 * Non-blocking TX: push to ring buffer, drained by timer ISR.
 *
 * Pins: P0_13 (RX), P0_14 (TX), AF1 on 983HH board.
 *
 * Baud rate formula: baud = pclk / (BRP+1) / NSPB
 *   Low clock  (<=8 MHz): NSPB=4  (LWBR=0x30)
 *   High clock (>8 MHz):  NSPB=16 (LWBR=0xF0)
 */

#include "hal_uart.h"
#include "hal_gpio.h"
#include "lib_ringbuf.h"
#include "board.h"

/* Ring buffer for non-blocking TX */
static ringbuf_t g_tx_rb;

static void uart_pins_init(void)
{
    uint16 mask = (uint16)((1u << BOARD_UART_TX_BIT) |
                           (1u << BOARD_UART_RX_BIT));

    /* AF1: PFC=0, PFCE=0, PFCAE=0 */
    PORTPFC0   &= ~mask;
    PORTPFCE0  &= ~mask;
    PORTPFCAE0 &= ~mask;

    /* TX = output, RX = input */
    PORTPMSR0 = PSR_CLR(BOARD_UART_TX_BIT);
    PORTPMSR0 = PSR_SET(BOARD_UART_RX_BIT);

    /* Enable alternate function */
    PORTPMC0 |= mask;
}

void hal_uart_init(uint32 pclk_hz, uint32 baud)
{
    uint8  lwbr;
    uint16 brp;
    uint32 nspb;

    /* Select sample count based on clock speed */
    if (pclk_hz <= 8000000u)
    {
        lwbr = 0x30u;       /* NSPB=3 -> 4 samples */
        nspb = 4u;
    }
    else
    {
        lwbr = 0xF0u;       /* NSPB=15 -> 16 samples */
        nspb = 16u;
    }

    /* BRP with rounding: (pclk + baud*nspb/2) / (baud*nspb) - 1 */
    brp = (uint16)((pclk_hz + baud * (nspb / 2u)) / (baud * nspb) - 1u);

    /* Disable and reset RLIN32 */
    RLN32LUOER = 0x00u;
    RLN32LCUC  = 0x00u;
    while (RLN32LMST & 0x01u)
        ;

    /* Configure UART mode, 8N1 */
    RLN32LMD   = 0x01u;     /* UART mode */
    RLN32LSC   = 0x00u;     /* No inter-byte space */
    RLN32LBFC  = 0x00u;     /* 8-bit, no parity, 1 stop, LSB first */
    RLN32LEDE  = 0x04u;     /* Overrun error detect */
    RLN32LUOR1 = 0x00u;     /* TX interrupt config */

    /* Set baud rate */
    RLN32LWBR   = lwbr;
    RLN32LBRP01 = brp;

    /* Exit reset, configure pins, enable */
    RLN32LCUC = 0x01u;
    uart_pins_init();
    RLN32LUOER = 0x03u;     /* Enable TX + RX */
}

void hal_uart_putc(uint8 c)
{
    /* Wait for transmitter idle (LST.UTS bit 4 = 0) */
    while (RLN32LST & 0x10u)
        ;
    RLN32LUTDR = (uint16)c;
}

void hal_uart_puts(const char *s)
{
    while (*s)
    {
        if (*s == '\n')
            hal_uart_putc('\r');
        hal_uart_putc((uint8)*s++);
    }
}

void hal_uart_put_hex8(uint8 val)
{
    static const char hex[] = "0123456789ABCDEF";
    hal_uart_putc(hex[(val >> 4) & 0x0Fu]);
    hal_uart_putc(hex[val & 0x0Fu]);
}

void hal_uart_put_hex32(uint32 val)
{
    hal_uart_put_hex8((uint8)(val >> 24));
    hal_uart_put_hex8((uint8)(val >> 16));
    hal_uart_put_hex8((uint8)(val >> 8));
    hal_uart_put_hex8((uint8)(val));
}

/* ---- Non-blocking API ---- */

void hal_uart_nb_init(void)
{
    ringbuf_init(&g_tx_rb);
}

void hal_uart_nb_putc(uint8 c)
{
    ringbuf_put(&g_tx_rb, c);   /* Drop silently if full */
}

void hal_uart_nb_puts(const char *s)
{
    while (*s)
    {
        if (*s == '\n')
            hal_uart_nb_putc('\r');
        hal_uart_nb_putc((uint8)*s++);
    }
}

void hal_uart_nb_put_hex8(uint8 val)
{
    static const char hex[] = "0123456789ABCDEF";
    hal_uart_nb_putc(hex[(val >> 4) & 0x0Fu]);
    hal_uart_nb_putc(hex[val & 0x0Fu]);
}

uint32 hal_uart_drain(uint32 max_bytes)
{
    uint32 count = 0u;
    uint8  byte;

    while (count < max_bytes)
    {
        /* Check if UART TX is idle (LST.UTS bit 4 = 0) */
        if (RLN32LST & 0x10u)
            break;              /* TX busy, try next time */

        if (!ringbuf_get(&g_tx_rb, &byte))
            break;              /* Buffer empty */

        RLN32LUTDR = (uint16)byte;
        count++;
    }

    return count;
}
