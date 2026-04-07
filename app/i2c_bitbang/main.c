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

static void delay(uint32 cnt)
{
    volatile uint32 d = cnt;
    while (d-- != 0u)
    {
        ;
    }
}

int main(void)
{
    uint8 val;

    BOOT_BANNER("i2c_bitbang");

    hal_i2c_bitbang_init();

    for (;;)
    {
        val = 0xFFu;
        (void)hal_i2c_bitbang_write(0x38u, &val, 1u);
        delay(500000);

        val = 0x00u;
        (void)hal_i2c_bitbang_write(0x38u, &val, 1u);
        delay(500000);
    }

    return 0;
}
