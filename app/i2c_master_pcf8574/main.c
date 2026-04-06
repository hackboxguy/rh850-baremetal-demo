/*
 * i2c_master_pcf8574 - Hardware I2C master to PCF8574A
 *
 * Toggles all 8 GPIOs of a PCF8574A using RIIC0 hardware I2C.
 * Requires PLL (80 MHz CPU, 40 MHz peripherals).
 * Debug output on UART (RLIN32, 115200 baud).
 *
 * PCF8574A address: 0x38 (7-bit, A0=A1=A2=GND)
 *
 * Build: make BOARD=983HH APP=i2c_master_pcf8574
 */

#include "board.h"
#include "hal_clock.h"
#include "hal_uart.h"
#include "hal_riic_master.h"

static void delay(volatile uint32 count)
{
    while (count--)
        ;
}

static void uart_reinit(void)
{
    hal_uart_init(BOARD_PCLK_HZ, BOARD_UART_BAUD);
}

int main(void)
{
    uint8 val;

    /* Early UART at 4 MHz (default clock) */
    hal_uart_init(BOARD_PCLK_NOPLL_HZ, BOARD_UART_BAUD);
    hal_uart_puts("\n=== I2C Master PCF8574 ===\n");

    /* PLL init (UART reinited at 40 MHz inside) */
    hal_clock_init(uart_reinit);
    hal_uart_puts("PLL done, CPU=80MHz\n");

    /* RIIC0 master init */
    hal_riic_master_init();
    hal_uart_puts("RIIC0 master ready\n");

    /* Blink loop */
    hal_uart_puts("Starting blink...\n");
    for (;;)
    {
        val = 0xFFu;
        if (hal_riic_master_write(0x38u, &val, 1u))
            hal_uart_puts("W:FF OK\n");
        else
            hal_uart_puts("W:FF FAIL\n");
        delay(4000000);     /* ~500 ms at 80 MHz */

        val = 0x00u;
        if (hal_riic_master_write(0x38u, &val, 1u))
            hal_uart_puts("W:00 OK\n");
        else
            hal_uart_puts("W:00 FAIL\n");
        delay(4000000);
    }

    return 0;
}
