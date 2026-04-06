/*
 * i2c_slave - I2C slave with register map (LED + DIP switches)
 *
 * RH850 acts as I2C slave (address 0x50) with register-based protocol:
 *   Register 0x00 (R/W): LED control (bit0 = P9_6)
 *   Register 0x01 (R):   DIP switches (bit0=AP0_7 ... bit7=AP0_14)
 *
 * Requires PLL (80 MHz CPU, 40 MHz peripherals).
 * Interrupt-driven RIIC0 slave mode.
 *
 * Pi4 usage:
 *   i2cdetect -y 1                    # Shows 0x50
 *   i2cset -y 1 0x50 0x00 0x01        # LED ON
 *   i2cset -y 1 0x50 0x00 0x00        # LED OFF
 *   i2cget -y 1 0x50 0x00             # Read LED state
 *   i2cget -y 1 0x50 0x01             # Read DIP switches
 *
 * Build:
 *   make BOARD=983HH APP=i2c_slave
 *   make BOARD=983HH APP=i2c_slave DEBUG=on
 */

#include "board.h"
#include "hal_clock.h"
#include "hal_uart.h"
#include "hal_gpio.h"
#include "hal_riic_slave.h"
#include "lib_debug.h"

/* Register map */
#define REG_LED     0x00u
#define REG_DIP     0x01u
#define REG_COUNT   2u

static volatile uint8 g_regs[REG_COUNT];

/* I2C slave write callback */
static void on_write(uint8 reg, uint8 val)
{
    if (reg < REG_COUNT)
        g_regs[reg] = val;
}

/* I2C slave read callback */
static uint8 on_read(uint8 reg)
{
    if (reg == REG_DIP)
        g_regs[REG_DIP] = hal_gpio_read_dip();
    return (reg < REG_COUNT) ? g_regs[reg] : 0xFFu;
}

static void uart_reinit(void)
{
    hal_uart_init(BOARD_PCLK_HZ, BOARD_UART_BAUD);
}

int main(void)
{
    /* Explicitly init globals (safety: BSS zeroing depends on linker setup) */
    g_regs[REG_LED] = 0x00u;
    g_regs[REG_DIP] = 0x00u;

    /* Early UART at 4 MHz */
    hal_uart_init(BOARD_PCLK_NOPLL_HZ, BOARD_UART_BAUD);
    hal_uart_puts("\n=== I2C Slave Demo ===\n");

    /* PLL init */
    hal_clock_init(uart_reinit);
    hal_uart_puts("PLL done, CPU=80MHz\n");

    /* Board-specific GPIO (LED + DIP switches) */
    board_init();

    /* RIIC0 slave init */
    hal_riic_slave_init(BOARD_I2C_SLAVE_ADDR, on_write, on_read);
    hal_uart_puts("RIIC0 slave ready, addr=0x");
    hal_uart_put_hex8(BOARD_I2C_SLAVE_ADDR);
    hal_uart_puts("\n");

    /* Enable global interrupts */
    __EI();
    hal_uart_puts("Ready.\n");

    /* Main loop: apply LED state and refresh DIP */
    for (;;)
    {
        hal_gpio_write(BOARD_LED_PORT, BOARD_LED_BIT,
                       g_regs[REG_LED] & 0x01u);
        g_regs[REG_DIP] = hal_gpio_read_dip();
    }

    return 0;
}
