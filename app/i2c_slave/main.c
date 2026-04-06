/*
 * i2c_slave - I2C slave with 16-bit register map (EEPROM-style)
 *
 * RH850 acts as I2C slave (address 0x50) with 16-bit sub-addressing:
 *
 *   Register map:
 *     0x0000-0x00FF  Device info (read-only)
 *       0x0000       Firmware version major (BCD, e.g. 0x01)
 *       0x0001       Firmware version minor (BCD, e.g. 0x10 = .10)
 *     0x0100-0x01FF  Status (read-only)
 *       0x0100       DIP switches (bit0=AP0_7 ... bit7=AP0_14)
 *     0x0200-0x02FF  Control (read/write)
 *       0x0200       LED control (bit0 = P9_6, 1=ON 0=OFF)
 *
 * Protocol (24C256/24C512 compatible):
 *   Write: [0x50+W] [addr_hi] [addr_lo] [data0] [data1] ...
 *   Read:  [0x50+W] [addr_hi] [addr_lo] [0x50+R] [data0] ...
 *   Current-address read: [0x50+R] [data0] ...
 *
 * Pi4 usage (i2ctransfer):
 *   # Read firmware version (2 bytes at 0x0000)
 *   i2ctransfer -y 1 w2@0x50 0x00 0x00 r2@0x50
 *
 *   # Read DIP switches (1 byte at 0x0100)
 *   i2ctransfer -y 1 w2@0x50 0x01 0x00 r1@0x50
 *
 *   # LED ON (write 0x01 to 0x0200)
 *   i2ctransfer -y 1 w3@0x50 0x02 0x00 0x01
 *
 *   # LED OFF (write 0x00 to 0x0200)
 *   i2ctransfer -y 1 w3@0x50 0x02 0x00 0x00
 *
 *   # Read LED state (1 byte at 0x0200)
 *   i2ctransfer -y 1 w2@0x50 0x02 0x00 r1@0x50
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
#include "hal_timer.h"
#include "lib_debug.h"

/* Firmware version (BCD): v1.00 */
#define FW_VERSION_MAJOR    0x01u
#define FW_VERSION_MINOR    0x00u

/* Register map pages */
#define PAGE_DEVINFO        0x0000u     /* 0x0000-0x00FF: Device info (RO) */
#define PAGE_STATUS         0x0100u     /* 0x0100-0x01FF: Status (RO) */
#define PAGE_CONTROL        0x0200u     /* 0x0200-0x02FF: Control (RW) */

/* Device info registers */
#define REG_FW_MAJOR        0x0000u     /* Firmware version major (BCD) */
#define REG_FW_MINOR        0x0001u     /* Firmware version minor (BCD) */

/* Status registers */
#define REG_DIP             0x0100u     /* DIP switches */

/* Control registers */
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
        break;              /* Ignore writes to unknown/read-only registers */
    }
}

/* I2C slave read callback (16-bit address) */
static uint8 on_read(uint16 reg)
{
    switch (reg)
    {
    /* Device info page */
    case REG_FW_MAJOR:  return FW_VERSION_MAJOR;
    case REG_FW_MINOR:  return FW_VERSION_MINOR;

    /* Status page */
    case REG_DIP:       return hal_gpio_read_dip();

    /* Control page */
    case REG_LED:       return g_led_state;

    default:            return 0xFFu;
    }
}

static void uart_reinit(void)
{
    hal_uart_init(BOARD_PCLK_HZ, BOARD_UART_BAUD);
}

/*
 * Timer callback: drain ring buffer to UART.
 * Called every 1 ms from OSTM0 ISR.
 */
static void timer_drain_cb(void)
{
    hal_uart_drain(8u);
}

int main(void)
{
    /* Explicitly init globals */
    g_led_state = 0x00u;

    /* Early UART at 4 MHz (blocking, for boot messages) */
    hal_uart_init(BOARD_PCLK_NOPLL_HZ, BOARD_UART_BAUD);
    hal_uart_puts("\n=== I2C Slave Demo (16-bit addr) ===\n");
    hal_uart_puts("FW v");
    hal_uart_put_hex8(FW_VERSION_MAJOR);
    hal_uart_putc('.');
    hal_uart_put_hex8(FW_VERSION_MINOR);
    hal_uart_putc('\n');

    /* PLL init */
    hal_clock_init(uart_reinit);
    hal_uart_puts("PLL done, CPU=80MHz\n");

    /* Init non-blocking debug ring buffer */
    hal_uart_nb_init();

    /* Start OSTM0 timer: 1 ms interval at 40 MHz, drains ring buffer */
    hal_timer_init(1000u, BOARD_PCLK_HZ, timer_drain_cb);
    hal_uart_puts("Timer started (1ms)\n");

    /* Board-specific GPIO (LED + DIP switches) */
    board_init();

    /* RIIC0 slave init (16-bit sub-addressing) */
    hal_riic_slave_init(BOARD_I2C_SLAVE_ADDR, on_write, on_read);
    hal_uart_puts("RIIC0 slave ready, addr=0x");
    hal_uart_put_hex8(BOARD_I2C_SLAVE_ADDR);
    hal_uart_puts("\n");

    /* Enable global interrupts */
    __EI();
    hal_uart_puts("Ready.\n");

    /* Main loop: apply LED state */
    for (;;)
    {
        hal_gpio_write(BOARD_LED_PORT, BOARD_LED_BIT,
                       g_led_state & 0x01u);
    }

    return 0;
}
