/*
 * display_manager - Remote display controller with PCL power control
 *
 * Initializes the REMOTE_DISP board and manages display power state
 * based on the PCL (Power Control Line) signal on AP0_4:
 *   PCL LOW  = display ON (power-up sequence, backlight, LCD)
 *   PCL HIGH = display OFF (reverse power-down sequence)
 *
 * On first boot, waits for PCL=LOW before powering up.
 * PCL transitions are debounced (50ms of stable state required).
 *
 * I2C slave address: 0x50, 16-bit sub-addressing (EEPROM-style).
 * See docs/i2c_register_map.md for protocol spec.
 *
 * Backlight temperature monitoring:
 *   NTC on AP0_0 sampled every 100ms (only when display is ON).
 *
 * Build:
 *   make BOARD=REMOTE_DISP APP=display_manager
 *   make BOARD=REMOTE_DISP APP=display_manager DEBUG=on
 *   make BOARD=REMOTE_DISP APP=display_manager VERSION=01.00
 */

#include "board.h"
#include "hal_clock.h"
#include "hal_gpio.h"
#include "hal_adc.h"
#include "hal_riic_slave.h"
#include "hal_i2c1_bitbang.h"
#include "hal_timer.h"
#include "hal_uart.h"
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

/* Page 0x01: Status */
#define REG_DISP_STATE      0x0100u     /* Display power state (0=OFF, 1=ON) RO */

/* Page 0x02: Control */
#define REG_DISP_POWER_CMD  0x0200u     /* Display power command (0=OFF, 1=ON) RW */

/* Page 0x03: Debug commands */
#define REG_DBG_CMD         0x0300u     /* Write 0x01=I2C1 scan, 0x00=clear */
#define REG_DBG_STATUS      0x0301u     /* 0=idle, 1=running, 2=done */

#define DBG_CMD_NONE        0x00u
#define DBG_CMD_I2C1_SCAN   0x01u

#define DBG_STATUS_IDLE     0x00u
#define DBG_STATUS_RUNNING  0x01u
#define DBG_STATUS_DONE     0x02u

/* Page 0x10: Diagnostics */
#define REG_TEMP_BL_RAW_HI  0x1000u
#define REG_TEMP_BL_RAW_LO  0x1001u
#define REG_TEMP_BL_DEG_HI  0x1002u
#define REG_TEMP_BL_DEG_LO  0x1003u

/* ---- Display power state machine ---- */

/*
 * Two sources control display power:
 *   PCL (AP0_4): hardware signal from vehicle (HIGH=OFF, LOW=ON)
 *   I2C cmd:     software command from head-unit (0x00=OFF, 0x01=ON)
 *
 * Priority: PCL=HIGH always forces OFF (vehicle safety).
 *           When PCL=LOW, I2C command controls ON/OFF.
 *
 *   Display ON  = (PCL=LOW) AND (i2c_cmd=ON)
 *   Display OFF = (PCL=HIGH) OR (i2c_cmd=OFF)
 */
#define DISP_OFF    0u
#define DISP_ON     1u

static volatile uint8  g_disp_state;
static volatile uint8  g_pcl_debounce;     /* Debounce counter (ms) */
static volatile uint8  g_pcl_last;         /* Last stable PCL state */
static volatile uint8  g_i2c_power_cmd;    /* I2C power command: 0=OFF, 1=ON */
static volatile uint8  g_dbg_cmd;          /* Debug command register */
static volatile uint8  g_dbg_status;       /* Debug status register */

/* ---- ADC / NTC conversion ---- */

static volatile uint16 g_adc_raw;
static volatile int16  g_temp_degc10;

static int32 approx_ln_x10000(uint32 r_ntc)
{
    int32 ratio_x1000;
    int32 ln_val;

    ratio_x1000 = (int32)((r_ntc * 1000u) / BOARD_NTC_R25);

    if (ratio_x1000 < 50)
    {
        ratio_x1000 = 50;
    }
    if (ratio_x1000 > 20000)
    {
        ratio_x1000 = 20000;
    }

    if (ratio_x1000 <= 100)
    {
        ln_val = -29957 + (((ratio_x1000 - 50) * 6931) / 50);
    }
    else if (ratio_x1000 <= 200)
    {
        ln_val = -23026 + (((ratio_x1000 - 100) * 6932) / 100);
    }
    else if (ratio_x1000 <= 500)
    {
        ln_val = -16094 + (((ratio_x1000 - 200) * 9163) / 300);
    }
    else if (ratio_x1000 <= 1000)
    {
        ln_val = -6931 + (((ratio_x1000 - 500) * 6931) / 500);
    }
    else if (ratio_x1000 <= 2000)
    {
        ln_val = ((ratio_x1000 - 1000) * 6931) / 1000;
    }
    else if (ratio_x1000 <= 5000)
    {
        ln_val = 6931 + (((ratio_x1000 - 2000) * 9163) / 3000);
    }
    else if (ratio_x1000 <= 10000)
    {
        ln_val = 16094 + (((ratio_x1000 - 5000) * 6932) / 5000);
    }
    else
    {
        ln_val = 23026 + (((ratio_x1000 - 10000) * 6931) / 10000);
    }

    return ln_val;
}

static int16 adc_to_temp_degc10(uint16 adc_raw)
{
    uint32 r_ntc;
    int32  ln_ratio;
    int32  inv_t;
    int32  t_kelvin;
    int32  t_celsius;

    if (adc_raw == 0u)
    {
        return 1500;
    }
    if (adc_raw >= 4095u)
    {
        return -400;
    }

    r_ntc = ((uint32)BOARD_NTC_R_PULLUP * (uint32)adc_raw) /
            (4095u - (uint32)adc_raw);

    ln_ratio = approx_ln_x10000(r_ntc);

    inv_t = 33540 + ((ln_ratio * 1000) / (int32)BOARD_NTC_BETA);

    if (inv_t <= 0)
    {
        return 1500;
    }

    t_kelvin = (int32)(100000000 / inv_t);
    t_celsius = t_kelvin - 2732;

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

/* ---- Timer callback (1ms) ---- */

static volatile uint8 g_adc_tick;

static void timer_callback(void)
{
    uint8 pcl_now;

#ifdef DEBUG_ENABLED
    (void)hal_uart_drain(8u);
#endif

    /* ---- PCL debounce (every 1ms tick) ---- */
    pcl_now = hal_gpio_read_ap0(PIN_PCL_BIT);
    if (pcl_now != g_pcl_last)
    {
        /* PCL changed — reset debounce counter */
        g_pcl_debounce = 0u;
        g_pcl_last = pcl_now;
    }
    else if (g_pcl_debounce < PCL_DEBOUNCE_MS)
    {
        g_pcl_debounce++;
    }
    else
    {
        /* Debounce complete — no action here, main loop checks state */
    }

    /* ---- ADC sampling (only when display is ON) ---- */
    if (g_disp_state == DISP_ON)
    {
        g_adc_tick++;
        if (g_adc_tick >= 100u)
        {
            g_adc_tick = 0u;
            g_adc_raw = hal_adc_read();
            g_temp_degc10 = adc_to_temp_degc10(g_adc_raw);
        }
    }
}

/* ---- I2C slave callbacks ---- */

static void on_write(uint16 reg, uint8 val)
{
    switch (reg)
    {
    case REG_DISP_POWER_CMD:
        g_i2c_power_cmd = (val != 0u) ? 1u : 0u;
        DBG_PUTS("I2C PWR=");
        DBG_HEX8(g_i2c_power_cmd);
        DBG_PUTS("\n");
        break;
    case REG_DBG_CMD:
        g_dbg_cmd = val;
        break;
    default:
        break;
    }
}

static uint8 on_read(uint16 reg)
{
    switch (reg)
    {
    case REG_FW_MAJOR:        return FW_VERSION_MAJOR;
    case REG_FW_MINOR:        return FW_VERSION_MINOR;
    case REG_DISP_STATE:      return g_disp_state;
    case REG_DISP_POWER_CMD:  return g_i2c_power_cmd;
    case REG_DBG_CMD:         return g_dbg_cmd;
    case REG_DBG_STATUS:      return g_dbg_status;
    case REG_TEMP_BL_RAW_HI:  return (uint8)(g_adc_raw >> 8);
    case REG_TEMP_BL_RAW_LO:  return (uint8)(g_adc_raw);
    case REG_TEMP_BL_DEG_HI:  return (uint8)((uint16)g_temp_degc10 >> 8);
    case REG_TEMP_BL_DEG_LO:  return (uint8)((uint16)g_temp_degc10);
    default:                  return 0xFFu;
    }
}

/* ---- Display power control ---- */

static volatile uint8 g_cold_boot = 1u;    /* 1 on first boot, 0 after */

static void display_power_on(void)
{
    if (g_cold_boot != 0u)
    {
        board_init();       /* Cold boot: port_init + power-on */
        g_cold_boot = 0u;
    }
    else
    {
        board_power_on();   /* Re-power: power-on only (no port_init) */
    }
    hal_adc_init(BOARD_NTC_ADC_CHANNEL);
    g_adc_raw = hal_adc_read();
    g_temp_degc10 = adc_to_temp_degc10(g_adc_raw);
    g_adc_tick = 0u;
    g_disp_state = DISP_ON;
    DBG_PUTS("DISP ON\n");
}

static void display_power_off(void)
{
    board_power_down();
    g_adc_raw = 0u;
    g_temp_degc10 = 0;
    g_disp_state = DISP_OFF;
    DBG_PUTS("DISP OFF\n");
}

/* ---- I2C1 bus scan (i2cdetect style) ---- */

static void i2c1_bus_scan(void)
{
    uint8 addr;
    uint8 col;

    /* Use blocking UART for the scan output (runs from main loop, not ISR) */
#ifdef DEBUG_ENABLED
    hal_uart_puts("I2C1 scan:\n");
    hal_uart_puts("     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f\n");

    for (addr = 0u; addr < 0x78u; addr++)
    {
        col = addr & 0x0Fu;

        /* Print row header */
        if (col == 0u)
        {
            hal_uart_put_hex8(addr);
            hal_uart_putc(':');
        }

        /* Skip reserved addresses 0x00-0x02 */
        if (addr < 0x03u)
        {
            hal_uart_puts("   ");
        }
        else if (hal_i2c1_bitbang_probe(addr) != 0u)
        {
            /* Device found */
            hal_uart_putc(' ');
            hal_uart_put_hex8(addr);
        }
        else
        {
            hal_uart_puts(" --");
        }

        /* End of row */
        if (col == 0x0Fu)
        {
            hal_uart_puts("\n");
        }
    }
    hal_uart_puts("\n");
#endif
}

/* ---- Main ---- */

int main(void)
{
    g_adc_raw = 0u;
    g_temp_degc10 = 0;
    g_adc_tick = 0u;
    g_disp_state = DISP_OFF;
    g_pcl_debounce = 0u;
    g_pcl_last = 1u;        /* Assume HIGH (display off) until proven otherwise */
    g_i2c_power_cmd = 1u;   /* Default ON — display turns on when PCL=LOW */
    g_dbg_cmd = DBG_CMD_NONE;
    g_dbg_status = DBG_STATUS_IDLE;

    /*
     * Assert 3.3V self-hold FIRST — before anything else.
     * PCL from vehicle may be a short pulse; the MCU must latch
     * its own power supply before the pulse ends.
     */
    hal_gpio_write(PIN_UG3V3_EN_PORT, PIN_UG3V3_EN_BIT, 1);
    hal_gpio_set_output(PIN_UG3V3_EN_PORT, PIN_UG3V3_EN_BIT);

    BOOT_BANNER("display_manager");

    /* PLL init */
    hal_clock_init(BOOT_UART_REINIT_CB);

#ifdef DEBUG_ENABLED
    hal_uart_puts("PLL done, CPU=80MHz\n");
    hal_uart_nb_init();
#endif

    /* Configure PCL input (AP0_4) before checking state */
    hal_gpio_set_ap0_input(PIN_PCL_BIT);

#ifdef DEBUG_ENABLED
    hal_uart_puts("PCL pin=");
    hal_uart_put_hex8(hal_gpio_read_ap0(PIN_PCL_BIT));
    hal_uart_puts("\n");
#endif

    /* Start timer (needed for UART drain + PCL debounce) */
    hal_timer_init(1000u, BOARD_PCLK_HZ, timer_callback);
#ifdef DEBUG_ENABLED
    hal_uart_puts("Timer started (1ms)\n");
#endif

    /* RIIC0 slave init (always active, even when display is off) */
    hal_riic_slave_init(BOARD_I2C_SLAVE_ADDR, on_write, on_read);
#ifdef DEBUG_ENABLED
    hal_uart_puts("RIIC0 slave addr=0x");
    hal_uart_put_hex8(BOARD_I2C_SLAVE_ADDR);
    hal_uart_puts(" (400kHz)\n");
#endif

    /* RIIC1 master init (I2C1 bus: deserializer, touch panel, scan) */
    hal_i2c1_bitbang_init();
#ifdef DEBUG_ENABLED
    hal_uart_puts("I2C1 bitbang ready (P8_0/P8_1)\n");
#endif

    /* Enable global interrupts */
    __EI();

    /* Check PCL at boot — only power up if LOW */
    if (hal_gpio_read_ap0(PIN_PCL_BIT) == 0u)
    {
#ifdef DEBUG_ENABLED
        hal_uart_puts("PCL=LOW, powering on\n");
#endif
        display_power_on();
    }
    else
    {
#ifdef DEBUG_ENABLED
        hal_uart_puts("PCL=HIGH, waiting\n");
#endif
    }

    /*
     * Main loop: monitor PCL + I2C for power state transitions.
     *
     * Priority logic:
     *   want_on = (PCL=LOW) AND (i2c_cmd=ON)
     *   PCL=HIGH always forces OFF (vehicle safety override).
     *   When PCL=LOW, I2C command controls ON/OFF.
     */
    for (;;)
    {
        /* ---- Display power state machine ---- */
        if (g_pcl_debounce >= PCL_DEBOUNCE_MS)
        {
            uint8 want_on;
            want_on = ((g_pcl_last == 0u) && (g_i2c_power_cmd != 0u))
                      ? 1u : 0u;

            if ((g_disp_state == DISP_OFF) && (want_on != 0u))
            {
                display_power_on();
            }
            else if ((g_disp_state == DISP_ON) && (want_on == 0u))
            {
                display_power_off();
            }
            else
            {
                /* No transition needed */
            }
        }

        /* ---- Debug command handler ---- */
        if (g_dbg_cmd != DBG_CMD_NONE)
        {
            uint8 cmd = g_dbg_cmd;
            g_dbg_cmd = DBG_CMD_NONE;

            switch (cmd)
            {
            case DBG_CMD_I2C1_SCAN:
                g_dbg_status = DBG_STATUS_RUNNING;
                i2c1_bus_scan();
                g_dbg_status = DBG_STATUS_DONE;
                break;
            default:
                break;
            }
        }
    }

    return 0;
}
