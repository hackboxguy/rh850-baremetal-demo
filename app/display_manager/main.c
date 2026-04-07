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
 * Backlight temperature monitoring:
 *   NTC on AP0_0 sampled every 100ms via OSTM0 timer.
 *   Raw ADC at register 0x1000-0x1001, temperature at 0x1002-0x1003.
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
#include "hal_adc.h"
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

/* ---- Register map ---- */

/* Page 0x00: Device info */
#define REG_FW_MAJOR        0x0000u
#define REG_FW_MINOR        0x0001u

/* Page 0x10: Diagnostics */
#define REG_TEMP_BL_RAW_HI  0x1000u     /* Backlight NTC ADC raw (bits 11:8) */
#define REG_TEMP_BL_RAW_LO  0x1001u     /* Backlight NTC ADC raw (bits 7:0) */
#define REG_TEMP_BL_DEG_HI  0x1002u     /* Temperature x10 degC (high byte) */
#define REG_TEMP_BL_DEG_LO  0x1003u     /* Temperature x10 degC (low byte) */

/* ---- ADC / NTC conversion ---- */

static volatile uint16 g_adc_raw;       /* Latest ADC reading (12-bit) */
static volatile int16  g_temp_degc10;   /* Temperature in 0.1 degC units */

/*
 * Convert 12-bit ADC reading to temperature (0.1 degC units).
 *
 * NTC in voltage divider: Vcc --- R_pullup --- ADC --- NTC --- GND
 *   R_ntc = R_pullup * adc / (4095 - adc)
 *
 * Simplified Beta equation (integer math, no float):
 *   1/T = 1/T25 + (1/Beta) * ln(R_ntc / R25)
 *   T (degC) = T (K) - 273.15
 *
 * We use a piecewise linear approximation of ln() to avoid
 * floating point. The ln(ratio) is computed via a lookup table
 * scaled by 10000.
 *
 * For the common range 0-85C with 10K NTC B=3950:
 *   ADC ~3300 at 0C, ~2048 at 25C, ~900 at 60C, ~500 at 85C
 */

/*
 * Integer ln(x * 1000) * 10000 approximation for x in [100..10000].
 * Uses the identity: ln(a/b) = ln(a) - ln(b)
 * We precompute ln(ratio) * 10000 for ratio = R_ntc / R25.
 *
 * Simplified: use linear interpolation between known points.
 * This avoids any floating point on the MCU.
 *
 * ln(0.1)*10000 = -23026
 * ln(0.5)*10000 = -6931
 * ln(1.0)*10000 = 0
 * ln(2.0)*10000 = 6931
 * ln(5.0)*10000 = 16094
 * ln(10.0)*10000 = 23026
 */

static int32 approx_ln_x10000(uint32 r_ntc)
{
    /*
     * Compute ln(R_ntc / R25) * 10000 using integer math.
     * ratio_x1000 = R_ntc * 1000 / R25
     *
     * Then use piecewise linear approximation of ln():
     *   ln(x) ~ (x-1) - (x-1)^2/2 for x near 1 (poor for large x)
     *
     * Better: use a simple lookup with interpolation.
     * We cover ratio 0.05 to 20.0 (50 to 20000 in x1000 units).
     */
    int32 ratio_x1000;
    int32 ln_val;

    ratio_x1000 = (int32)((r_ntc * 1000u) / BOARD_NTC_R25);

    /* Clamp to valid range */
    if (ratio_x1000 < 50)
    {
        ratio_x1000 = 50;
    }
    if (ratio_x1000 > 20000)
    {
        ratio_x1000 = 20000;
    }

    /*
     * Piecewise ln() * 10000:
     *   ratio_x1000=50    -> ln(0.05)*10000  = -29957
     *   ratio_x1000=100   -> ln(0.1)*10000   = -23026
     *   ratio_x1000=200   -> ln(0.2)*10000   = -16094
     *   ratio_x1000=500   -> ln(0.5)*10000   = -6931
     *   ratio_x1000=1000  -> ln(1.0)*10000   = 0
     *   ratio_x1000=2000  -> ln(2.0)*10000   = 6931
     *   ratio_x1000=5000  -> ln(5.0)*10000   = 16094
     *   ratio_x1000=10000 -> ln(10.0)*10000  = 23026
     *   ratio_x1000=20000 -> ln(20.0)*10000  = 29957
     */
    if (ratio_x1000 <= 100)
    {
        /* 50..100: lerp between -29957 and -23026 */
        ln_val = -29957 + (((ratio_x1000 - 50) * 6931) / 50);
    }
    else if (ratio_x1000 <= 200)
    {
        /* 100..200: lerp between -23026 and -16094 */
        ln_val = -23026 + (((ratio_x1000 - 100) * 6932) / 100);
    }
    else if (ratio_x1000 <= 500)
    {
        /* 200..500: lerp between -16094 and -6931 */
        ln_val = -16094 + (((ratio_x1000 - 200) * 9163) / 300);
    }
    else if (ratio_x1000 <= 1000)
    {
        /* 500..1000: lerp between -6931 and 0 */
        ln_val = -6931 + (((ratio_x1000 - 500) * 6931) / 500);
    }
    else if (ratio_x1000 <= 2000)
    {
        /* 1000..2000: lerp between 0 and 6931 */
        ln_val = ((ratio_x1000 - 1000) * 6931) / 1000;
    }
    else if (ratio_x1000 <= 5000)
    {
        /* 2000..5000: lerp between 6931 and 16094 */
        ln_val = 6931 + (((ratio_x1000 - 2000) * 9163) / 3000);
    }
    else if (ratio_x1000 <= 10000)
    {
        /* 5000..10000: lerp between 16094 and 23026 */
        ln_val = 16094 + (((ratio_x1000 - 5000) * 6932) / 5000);
    }
    else
    {
        /* 10000..20000: lerp between 23026 and 29957 */
        ln_val = 23026 + (((ratio_x1000 - 10000) * 6931) / 10000);
    }

    return ln_val;
}

static int16 adc_to_temp_degc10(uint16 adc_raw)
{
    uint32 r_ntc;
    int32  ln_ratio;
    int32  inv_t;      /* 1/T * 10000000 */
    int32  t_kelvin;   /* T in Kelvin * 10 */
    int32  t_celsius;  /* T in Celsius * 10 */

    /* Avoid divide by zero */
    if (adc_raw == 0u)
    {
        return 1500;    /* 150.0C (sensor short) */
    }
    if (adc_raw >= 4095u)
    {
        return -400;    /* -40.0C (sensor open) */
    }

    /* R_ntc = R_pullup * adc / (4095 - adc) */
    r_ntc = ((uint32)BOARD_NTC_R_PULLUP * (uint32)adc_raw) /
            (4095u - (uint32)adc_raw);

    /* ln(R_ntc / R25) * 10000 */
    ln_ratio = approx_ln_x10000(r_ntc);

    /*
     * Beta equation: 1/T = 1/T25 + ln(R/R25) / Beta
     *
     * In scaled integers (all * 10000000):
     *   inv_t25 = 10000000 * 100 / T25_K_x100 = 1000000000 / 29815 = 33540
     *   inv_t = inv_t25 + (ln_ratio * 10000 / Beta) * (10000000/10000)
     *         = inv_t25 + ln_ratio * 1000 / Beta
     *
     *   T_kelvin_x10 = 10000000 * 10 / inv_t = 100000000 / inv_t
     *   T_celsius_x10 = T_kelvin_x10 - 2732
     */

    /* 1/T25 scaled: 10000000 * 100 / 29815 = ~33540 */
    inv_t = 33540 + ((ln_ratio * 1000) / (int32)BOARD_NTC_BETA);

    if (inv_t <= 0)
    {
        return 1500;    /* Overflow protection */
    }

    t_kelvin = (int32)(100000000 / inv_t);
    t_celsius = t_kelvin - 2732;

    /* Clamp to reasonable range: -40.0 to 150.0 C */
    if (t_celsius < -400)
    {
        t_celsius = -400;
    }
    if (t_celsius > 1500)
    {
        t_celsius = 1500;
    }

    return (int16)t_celsius;
}

/* ---- Timer callback ---- */

static volatile uint8 g_adc_tick;   /* Counts 1ms ticks for 100ms ADC interval */
static volatile uint8 g_log_tick;   /* Counts ADC samples for 1s debug log */

static void timer_callback(void)
{
#ifdef DEBUG_ENABLED
    (void)hal_uart_drain(8u);
#endif

    /* Sample ADC every 100ms (100 ticks at 1ms interval) */
    g_adc_tick++;
    if (g_adc_tick >= 100u)
    {
        g_adc_tick = 0u;
        g_adc_raw = hal_adc_read();
        g_temp_degc10 = adc_to_temp_degc10(g_adc_raw);

        /* Log every 1s (every 10th ADC sample) */
        g_log_tick++;
        if (g_log_tick >= 10u)
        {
            g_log_tick = 0u;
            DBG_PUTS("ADC=");
            DBG_HEX8((uint8)(g_adc_raw >> 8));
            DBG_HEX8((uint8)g_adc_raw);
            DBG_PUTS(" T=");
            DBG_HEX8((uint8)((uint16)g_temp_degc10 >> 8));
            DBG_HEX8((uint8)((uint16)g_temp_degc10));
            DBG_PUTS("\n");
        }
    }
}

/* ---- I2C slave callbacks ---- */

static void on_write(uint16 reg, uint8 val)
{
    (void)reg;
    (void)val;
}

static uint8 on_read(uint16 reg)
{
    switch (reg)
    {
    /* Device info */
    case REG_FW_MAJOR:      return FW_VERSION_MAJOR;
    case REG_FW_MINOR:      return FW_VERSION_MINOR;

    /* Diagnostics: backlight temperature */
    case REG_TEMP_BL_RAW_HI:  return (uint8)(g_adc_raw >> 8);
    case REG_TEMP_BL_RAW_LO:  return (uint8)(g_adc_raw);
    case REG_TEMP_BL_DEG_HI:  return (uint8)((uint16)g_temp_degc10 >> 8);
    case REG_TEMP_BL_DEG_LO:  return (uint8)((uint16)g_temp_degc10);

    default:                return 0xFFu;
    }
}

/* ---- Main ---- */

int main(void)
{
    g_adc_raw = 0u;
    g_temp_degc10 = 0;
    g_adc_tick = 0u;
    g_log_tick = 0u;

    BOOT_BANNER("display_manager");

    /* PLL init */
    hal_clock_init(BOOT_UART_REINIT_CB);

#ifdef DEBUG_ENABLED
    hal_uart_puts("PLL done, CPU=80MHz\n");
    hal_uart_nb_init();
#endif

    /* Board power-up sequence */
    board_init();
#ifdef DEBUG_ENABLED
    hal_uart_puts("Board init done\n");
#endif

    /* ADC init (backlight NTC on AP0_0) */
    hal_adc_init(BOARD_NTC_ADC_CHANNEL);

    /* Take first reading before I2C goes live */
    g_adc_raw = hal_adc_read();
    g_temp_degc10 = adc_to_temp_degc10(g_adc_raw);

#ifdef DEBUG_ENABLED
    hal_uart_puts("ADC init, NTC raw=0x");
    hal_uart_put_hex8((uint8)(g_adc_raw >> 8));
    hal_uart_put_hex8((uint8)g_adc_raw);
    hal_uart_puts("\n");
#endif

    /* Start timer: 1ms interval, handles UART drain + 100ms ADC sampling */
    hal_timer_init(1000u, BOARD_PCLK_HZ, timer_callback);
#ifdef DEBUG_ENABLED
    hal_uart_puts("Timer started (1ms)\n");
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

    /* Main loop — idle. Timer handles ADC sampling. */
    for (;;)
    {
        ;
    }

    return 0;
}
