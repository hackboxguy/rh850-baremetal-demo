/*
 * i2c_master_pcf8574 - Hardware I2C master to PCF8574A
 *
 * Toggles all 8 GPIOs of a PCF8574A using RIIC0 hardware I2C.
 * Requires PLL (80 MHz CPU, 40 MHz peripherals).
 *
 * PCF8574A address: 0x38 (7-bit, A0=A1=A2=GND)
 *
 * Build:
 *   make BOARD=983HH APP=i2c_master_pcf8574
 *   make BOARD=983HH APP=i2c_master_pcf8574 DEBUG=on  (enables UART output)
 */

#include "board.h"
#include "hal_clock.h"
#include "hal_riic_master.h"
#include "lib_boot.h"
#include "lib_debug.h"

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

    BOOT_BANNER("i2c_master_pcf8574");

    /* PLL init (UART reinited at 40 MHz if DEBUG is on) */
    hal_clock_init(BOOT_UART_REINIT_CB);
    DBG_PUTS("PLL done, CPU=80MHz\n");

    /* RIIC0 master init */
    hal_riic_master_init();
    DBG_PUTS("RIIC0 master ready\n");

    /* Blink loop */
    for (;;)
    {
        val = 0xFFu;
        (void)hal_riic_master_write(0x38u, &val, 1u);
        delay(4000000);     /* ~500 ms at 80 MHz */

        val = 0x00u;
        (void)hal_riic_master_write(0x38u, &val, 1u);
        delay(4000000);
    }

    return 0;
}
