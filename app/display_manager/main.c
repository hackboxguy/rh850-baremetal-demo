/*
 * display_manager - Remote display controller
 *
 * Initializes the REMOTE_DISP board (power rails, FPGA, deserializer,
 * LCD panel, backlight) and runs an I2C slave interface for external
 * host communication.
 *
 * I2C slave address: 0x50, 16-bit sub-addressing (EEPROM-style).
 * See docs/i2c_register_map.md for protocol spec.
 *
 * Requires PLL (80 MHz CPU, 40 MHz peripherals).
 * I2C0 at 400 kHz fast mode.
 *
 * Build:
 *   make BOARD=REMOTE_DISP APP=display_manager
 *   make BOARD=REMOTE_DISP APP=display_manager DEBUG=on
 *   make BOARD=REMOTE_DISP APP=display_manager VERSION=01.00
 */

#include "board.h"
#include "hal_clock.h"
#include "hal_gpio.h"
#include "hal_riic_slave.h"
#include "hal_timer.h"
#include "lib_boot.h"
#include "lib_debug.h"

/* Firmware version (BCD) — provided by Makefile via -D flags. */
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

/* I2C slave write callback */
static void on_write(uint16 reg, uint8 val)
{
    /* No writable registers yet — future: backlight PWM, display mode */
    (void)reg;
    (void)val;
}

/* I2C slave read callback */
static uint8 on_read(uint16 reg)
{
    switch (reg)
    {
    case REG_FW_MAJOR:  return FW_VERSION_MAJOR;
    case REG_FW_MINOR:  return FW_VERSION_MINOR;
    case REG_DIP:       return hal_gpio_read_dip();
    default:            return 0xFFu;
    }
}

#ifdef DEBUG_ENABLED
static void timer_drain_cb(void)
{
    (void)hal_uart_drain(8u);
}
#endif

int main(void)
{
    BOOT_BANNER("display_manager");

    /* PLL init */
    hal_clock_init(BOOT_UART_REINIT_CB);
    DBG_PUTS("PLL done, CPU=80MHz\n");

#ifdef DEBUG_ENABLED
    hal_uart_nb_init();
    hal_timer_init(1000u, BOARD_PCLK_HZ, timer_drain_cb);
    hal_uart_puts("Timer started (1ms)\n");
#endif

    /* Board power-up sequence (power rails, FPGA, deser, LCD, backlight) */
    board_init();
#ifdef DEBUG_ENABLED
    hal_uart_puts("Board init done\n");
#endif

    /* RIIC0 slave init (400 kHz, 16-bit sub-addressing) */
    hal_riic_slave_init(BOARD_I2C_SLAVE_ADDR, on_write, on_read);
#ifdef DEBUG_ENABLED
    hal_uart_puts("RIIC0 slave addr=0x");
    hal_uart_put_hex8(BOARD_I2C_SLAVE_ADDR);
    hal_uart_puts(" (400kHz)\n");
#endif

    /* Enable global interrupts */
    __EI();

    /* Main loop — idle for now.
     * Future: message loop with timer workers for ADC sampling,
     * diagnostics, display status monitoring, etc. */
    for (;;)
    {
        ;
    }

    return 0;
}
