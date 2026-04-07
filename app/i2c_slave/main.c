/*
 * i2c_slave - I2C slave with 16-bit register map (EEPROM-style)
 *
 * RH850 acts as I2C slave (address 0x50) with 16-bit sub-addressing.
 * See docs/i2c_register_map.md for protocol and register layout.
 *
 * Requires PLL (80 MHz CPU, 40 MHz peripherals).
 * Interrupt-driven RIIC0 slave mode.
 *
 * Pi4 usage (i2ctransfer):
 *   i2ctransfer -y 1 w2@0x50 0x00 0x00 r2@0x50     # FW version
 *   i2ctransfer -y 1 w3@0x50 0x02 0x00 0x01         # LED ON
 *   i2ctransfer -y 1 w3@0x50 0x02 0x00 0x00         # LED OFF
 *   i2ctransfer -y 1 w2@0x50 0x01 0x00 r1@0x50      # DIP switches
 *
 * Build:
 *   make BOARD=983HH APP=i2c_slave
 *   make BOARD=983HH APP=i2c_slave DEBUG=on  (enables UART + ring buffer debug)
 */

#include "board.h"
#include "hal_clock.h"
#include "hal_gpio.h"
#include "hal_riic_slave.h"
#include "hal_timer.h"
#include "lib_boot.h"
#include "lib_debug.h"

/* Firmware version (BCD) — provided by Makefile via -D flags.
 * Build with: make VERSION=01.10  -> v1.10 (0x01, 0x10)
 * Defaults to v0.00 if VERSION not specified. */
#ifndef FW_VERSION_MAJOR
#define FW_VERSION_MAJOR    0x00u
#endif
#ifndef FW_VERSION_MINOR
#define FW_VERSION_MINOR    0x00u
#endif

/* Register map pages */
#define REG_FW_MAJOR        0x0000u     /* Firmware version major (BCD) */
#define REG_FW_MINOR        0x0001u     /* Firmware version minor (BCD) */
#define REG_DIP             0x0100u     /* DIP switches */
#define REG_LED             0x0200u     /* LED control (bit0 = P9_6) */

static volatile uint8 g_led_state;

/* I2C slave write callback (16-bit address) */
static void on_write(uint16 reg, uint8 val)
{
    switch (reg)
    {
    case REG_LED:
        g_led_state = val;
        break;
    default:
        break;
    }
}

/* I2C slave read callback (16-bit address) */
static uint8 on_read(uint16 reg)
{
    switch (reg)
    {
    case REG_FW_MAJOR:  return FW_VERSION_MAJOR;
    case REG_FW_MINOR:  return FW_VERSION_MINOR;
    case REG_DIP:       return hal_gpio_read_dip();
    case REG_LED:       return g_led_state;
    default:            return 0xFFu;
    }
}

#ifdef DEBUG_ENABLED
/*
 * Timer callback: drain ring buffer to UART.
 * Called every 1 ms from OSTM0 ISR.
 */
static void timer_drain_cb(void)
{
    (void)hal_uart_drain(8u);
}
#endif

int main(void)
{
    /* Explicitly init globals */
    g_led_state = 0x00u;

    BOOT_BANNER("i2c_slave");

    /* PLL init (UART reinited at 40 MHz if DEBUG is on) */
    hal_clock_init(BOOT_UART_REINIT_CB);
    DBG_PUTS("PLL done, CPU=80MHz\n");

#ifdef DEBUG_ENABLED
    /* Init non-blocking debug ring buffer + timer drain */
    hal_uart_nb_init();
    hal_timer_init(1000u, BOARD_PCLK_HZ, timer_drain_cb);
    DBG_PUTS("Timer started (1ms)\n");
#endif

    /* Board-specific GPIO (LED + DIP switches) */
    board_init();

    /* RIIC0 slave init (16-bit sub-addressing) */
    hal_riic_slave_init(BOARD_I2C_SLAVE_ADDR, on_write, on_read);
    DBG_PUTS("RIIC0 slave addr=0x");
    DBG_HEX8(BOARD_I2C_SLAVE_ADDR);
    DBG_PUTS("\n");

    /* Enable global interrupts */
    __EI();

    /* Main loop: apply LED state */
    for (;;)
    {
        hal_gpio_write(BOARD_LED_PORT, BOARD_LED_BIT,
                       g_led_state & 0x01u);
    }

    return 0;
}
