/*
 * i2c_bitbang - Bit-banged I2C to PCF8574A
 *
 * Toggles all 8 GPIOs of a PCF8574A I2C expander using
 * bit-banged I2C (no hardware RIIC peripheral needed).
 * No PLL required - runs on default 8 MHz HS IntOSC.
 *
 * PCF8574A address: 0x38 (7-bit, A0=A1=A2=GND)
 *
 * Build:
 *   make BOARD=983HH APP=i2c_bitbang
 *   make BOARD=983HH APP=i2c_bitbang DEBUG=on  (enables UART boot banner)
 */

#include "board.h"
#include "hal_i2c_bitbang.h"
#include "lib_boot.h"

#define PCF8574A_ADDR       0x38u
#define RETRY_DELAY_COUNT   20000u

static void delay(uint32 cnt)
{
    volatile uint32 d = cnt;
    while (d-- != 0u)
    {
        ;
    }
}

static void pcf8574a_wait_ready(void)
{
    uint8 val = 0xFFu;

    for (;;)
    {
        hal_i2c_bitbang_init();
        if (hal_i2c_bitbang_write(PCF8574A_ADDR, &val, 1u) != 0u)
        {
            return;
        }
        delay(RETRY_DELAY_COUNT);
    }
}

static void pcf8574a_write_blocking(uint8 val)
{
    for (;;)
    {
        if (hal_i2c_bitbang_write(PCF8574A_ADDR, &val, 1u) != 0u)
        {
            return;
        }
        hal_i2c_bitbang_init();
        delay(RETRY_DELAY_COUNT);
    }
}

int main(void)
{
    uint8 val;

    BOOT_BANNER("i2c_bitbang");

    hal_i2c_bitbang_init();
    pcf8574a_wait_ready();

    for (;;)
    {
        val = 0xFFu;
        pcf8574a_write_blocking(val);
        delay(500000);

        val = 0x00u;
        pcf8574a_write_blocking(val);
        delay(500000);
    }

    return 0;
}
