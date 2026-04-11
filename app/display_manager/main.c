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
 * I2C slave address: 0x66, 16-bit sub-addressing (EEPROM-style).
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
#include "lib_buildinfo.h"
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
#define REG_BUILD_YEAR_HI   0x0002u
#define REG_BUILD_YEAR_LO   0x0003u
#define REG_BUILD_MONTH     0x0004u
#define REG_BUILD_DAY       0x0005u
#define REG_BUILD_HOUR      0x0006u
#define REG_BUILD_MINUTE    0x0007u

/* Page 0x01: Status */
#define REG_DISP_STATE      0x0100u     /* Display power state (0=OFF, 1=ON) RO */

/* Page 0x02: Control */
#define REG_DISP_POWER_CMD  0x0200u     /* Display power command (0=OFF, 1=ON) RW */
#define REG_LCD_TP_RST      0x0201u     /* Touch panel reset: 0=LOW(assert), 1=HIGH(release) RW */

/* Page 0x03: Debug commands */
#define REG_DBG_CMD         0x0300u     /* Debug command register */
#define REG_DBG_STATUS      0x0301u     /* 0=idle, 1=running, 2=done */
#define REG_DBG_I2C_LOG     0x0302u     /* 0=disable slave debug, 1=enable */
#define REG_SCAN_DEV_COUNT  0x0303u     /* Number of devices found (RO) */
#define REG_SCAN_BUF_START  0x0380u     /* Scan buffer: 128 bytes (0x0380-0x03FF) */
#define REG_SCAN_BUF_END    0x03FFu

#define DBG_CMD_NONE        0x00u
#define DBG_CMD_I2C1_PRINT  0x01u       /* Scan + print to UART (DEBUG builds) */
#define DBG_CMD_I2C1_SCAN   0x02u       /* Scan to buffer (all builds) */
#define DBG_CMD_SCAN_FLUSH  0x03u       /* Clear scan buffer */

#define DBG_STATUS_IDLE     0x00u
#define DBG_STATUS_RUNNING  0x01u
#define DBG_STATUS_DONE     0x02u

#define SCAN_BUF_SIZE       128u        /* 7-bit address space: 0x00-0x7F */

/* I2C bridge registers (0x0310-0x032F) */
#define REG_BRIDGE_SLAVE    0x0310u     /* Target 7-bit slave address */
#define REG_BRIDGE_REG      0x0311u     /* Target register address */
#define REG_BRIDGE_LEN      0x0312u     /* Bytes to read/write (1-16) */
#define REG_BRIDGE_CMD      0x0313u     /* 0x00=idle, 0x01=read, 0x02=write */
#define REG_BRIDGE_STATUS   0x0314u     /* 0x00=idle, 0x01=running, 0x02=done, 0xFF=error */
#define REG_BRIDGE_DATA     0x0320u     /* 16-byte data buffer (0x0320-0x032F) */
#define REG_BRIDGE_DATA_END 0x032Fu

#define BRIDGE_CMD_NONE     0x00u
#define BRIDGE_CMD_READ     0x01u
#define BRIDGE_CMD_WRITE    0x02u

#define BRIDGE_STATUS_IDLE  0x00u
#define BRIDGE_STATUS_RUN   0x01u
#define BRIDGE_STATUS_DONE  0x02u
#define BRIDGE_STATUS_ERR   0xFFu

#define BRIDGE_DATA_SIZE    16u

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
static volatile uint8  g_pcl_debounce;     /* Debounce counter (ms) — ISR only */
static volatile uint8  g_pcl_last;         /* Last sampled PCL state — ISR only */
static volatile uint8  g_pcl_stable;       /* Debounced stable PCL state (ISR -> main) */
static volatile uint8  g_i2c_power_cmd;    /* I2C power command: 0=OFF, 1=ON */
static volatile uint8  g_dbg_cmd;          /* Debug command register */
static volatile uint8  g_dbg_status;       /* Debug status register */
static volatile uint8  g_dbg_i2c_log;      /* I2C slave debug: 1=on, 0=off */
static volatile uint8  g_scan_buf[SCAN_BUF_SIZE]; /* 0=no device, 1=ACK */
static volatile uint8  g_scan_dev_count;   /* Number of devices found */

/* I2C bridge state */
static volatile uint8  g_bridge_slave;     /* Target 7-bit slave address */
static volatile uint8  g_bridge_reg;       /* Target register address */
static volatile uint8  g_bridge_len;       /* Bytes to read/write (1-16) */
static volatile uint8  g_bridge_cmd;       /* Command: 0=idle, 1=read, 2=write */
static volatile uint8  g_bridge_status;    /* Status: 0=idle, 1=run, 2=done, FF=err */
static volatile uint8  g_bridge_data[BRIDGE_DATA_SIZE]; /* Data buffer */

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

    /* ---- PCL debounce (every 1ms tick) ----
     * Debounce logic runs entirely in this ISR. The main loop reads
     * only g_pcl_stable, which atomically reflects the most recently
     * debounced PCL value. This avoids the race where main reads
     * debounce counter and stable state separately. */
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
        if (g_pcl_debounce >= PCL_DEBOUNCE_MS)
        {
            /* Debounce just completed — publish stable value to main */
            g_pcl_stable = pcl_now;
        }
    }
    else
    {
        /* Already debounced and stable — no action */
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
    case REG_LCD_TP_RST:
        hal_gpio_write(PIN_LCD_TP_RST_PORT, PIN_LCD_TP_RST_BIT,
                       (val != 0u) ? 1u : 0u);
        break;
    case REG_DBG_CMD:
        g_dbg_cmd = val;
        break;
    case REG_DBG_I2C_LOG:
        g_dbg_i2c_log = (val != 0u) ? 1u : 0u;
        g_riic_slave_dbg_en = g_dbg_i2c_log;
        break;
    /* I2C bridge registers */
    case REG_BRIDGE_SLAVE:
        g_bridge_slave = val;
        break;
    case REG_BRIDGE_REG:
        g_bridge_reg = val;
        break;
    case REG_BRIDGE_LEN:
        g_bridge_len = (val > BRIDGE_DATA_SIZE) ? BRIDGE_DATA_SIZE : val;
        break;
    case REG_BRIDGE_CMD:
        g_bridge_cmd = val;
        break;
    default:
        /* Bridge data buffer: 0x0320-0x032F */
        if ((reg >= REG_BRIDGE_DATA) && (reg <= REG_BRIDGE_DATA_END))
        {
            g_bridge_data[reg - REG_BRIDGE_DATA] = val;
        }
        break;
    }
}

static uint8 on_read(uint16 reg)
{
    switch (reg)
    {
    case REG_FW_MAJOR:        return FW_VERSION_MAJOR;
    case REG_FW_MINOR:        return FW_VERSION_MINOR;
    case REG_BUILD_YEAR_HI:   return BUILD_YEAR_HI;
    case REG_BUILD_YEAR_LO:   return BUILD_YEAR_LO;
    case REG_BUILD_MONTH:     return BUILD_MONTH;
    case REG_BUILD_DAY:       return BUILD_DAY;
    case REG_BUILD_HOUR:      return BUILD_HOUR;
    case REG_BUILD_MINUTE:    return BUILD_MINUTE;
    case REG_DISP_STATE:      return g_disp_state;
    case REG_DISP_POWER_CMD:  return g_i2c_power_cmd;
    case REG_LCD_TP_RST:      return hal_gpio_read(PIN_LCD_TP_RST_PORT, PIN_LCD_TP_RST_BIT);
    case REG_DBG_CMD:         return g_dbg_cmd;
    case REG_DBG_STATUS:      return g_dbg_status;
    case REG_DBG_I2C_LOG:     return g_dbg_i2c_log;
    case REG_SCAN_DEV_COUNT:  return g_scan_dev_count;
    /* I2C bridge registers */
    case REG_BRIDGE_SLAVE:    return g_bridge_slave;
    case REG_BRIDGE_REG:      return g_bridge_reg;
    case REG_BRIDGE_LEN:      return g_bridge_len;
    case REG_BRIDGE_CMD:      return g_bridge_cmd;
    case REG_BRIDGE_STATUS:   return g_bridge_status;
    /* Diagnostics */
    case REG_TEMP_BL_RAW_HI:  return (uint8)(g_adc_raw >> 8);
    case REG_TEMP_BL_RAW_LO:  return (uint8)(g_adc_raw);
    case REG_TEMP_BL_DEG_HI:  return (uint8)((uint16)g_temp_degc10 >> 8);
    case REG_TEMP_BL_DEG_LO:  return (uint8)((uint16)g_temp_degc10);
    default:
        /* Bridge data buffer: 0x0320-0x032F */
        if ((reg >= REG_BRIDGE_DATA) && (reg <= REG_BRIDGE_DATA_END))
        {
            return g_bridge_data[reg - REG_BRIDGE_DATA];
        }
        /* Scan buffer: 0x0380-0x03FF */
        if ((reg >= REG_SCAN_BUF_START) && (reg <= REG_SCAN_BUF_END))
        {
            return g_scan_buf[reg - REG_SCAN_BUF_START];
        }
        return 0xFFu;
    }
}

/* ---- Display power control ---- */

static void display_power_on(void)
{
    static volatile uint8 cold_boot = 1u;   /* 1 on first boot, 0 after */

    if (cold_boot != 0u)
    {
        board_init();       /* Cold boot: port_init + power-on */
        cold_boot = 0u;
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

/* ---- I2C bridge execution ---- */

static void bridge_execute(void)
{
    uint8 ok;
    uint8 i;

    if (g_bridge_len == 0u)
    {
        g_bridge_status = BRIDGE_STATUS_ERR;
        return;
    }
    if (g_bridge_len > BRIDGE_DATA_SIZE)
    {
        g_bridge_status = BRIDGE_STATUS_ERR;
        return;
    }

    g_bridge_status = BRIDGE_STATUS_RUN;

    if (g_bridge_cmd == BRIDGE_CMD_READ)
    {
        /*
         * I2C register read with repeated-start (no STOP between phases):
         *   START + [slave+W] + [reg_addr] + RESTART + [slave+R] + [data] + STOP
         *
         * Required for slaves that do not preserve the register pointer
         * across STOP conditions.
         */
        uint8 reg_addr = g_bridge_reg;
        uint8 tmp[BRIDGE_DATA_SIZE];
        ok = hal_i2c1_bitbang_write_read(g_bridge_slave,
                                         &reg_addr, 1u,
                                         tmp, g_bridge_len);
        if (ok != 0u)
        {
            for (i = 0u; i < g_bridge_len; i++)
            {
                g_bridge_data[i] = tmp[i];
            }
        }
    }
    else if (g_bridge_cmd == BRIDGE_CMD_WRITE)
    {
        /*
         * I2C write: send register address + data in one transaction.
         * Sequence: START + [slave+W] + [reg_addr] + [data...] + STOP
         */
        uint8 tmp[BRIDGE_DATA_SIZE + 1u];
        tmp[0] = g_bridge_reg;
        for (i = 0u; i < g_bridge_len; i++)
        {
            tmp[i + 1u] = g_bridge_data[i];
        }
        ok = hal_i2c1_bitbang_write(g_bridge_slave, tmp, g_bridge_len + 1u);
    }
    else
    {
        ok = 0u;
    }

    g_bridge_status = (ok != 0u) ? BRIDGE_STATUS_DONE : BRIDGE_STATUS_ERR;
}

/* ---- I2C1 bus scan ---- */

static void scan_flush(void)
{
    uint8 i;
    for (i = 0u; i < SCAN_BUF_SIZE; i++)
    {
        g_scan_buf[i] = 0u;
    }
    g_scan_dev_count = 0u;
    g_dbg_status = DBG_STATUS_IDLE;
}

/*
 * Scan I2C1 bus into buffer. Works in both release and debug builds.
 * Results stored in g_scan_buf[0..127]: 0=no device, 1=ACK.
 */
static void i2c1_scan_to_buffer(void)
{
    uint8 addr;
    uint8 count = 0u;

    for (addr = 0u; addr < SCAN_BUF_SIZE; addr++)
    {
        if (addr < 0x03u)
        {
            g_scan_buf[addr] = 0u;  /* Reserved addresses */
        }
        else if (addr >= 0x78u)
        {
            g_scan_buf[addr] = 0u;  /* Reserved 10-bit prefix range */
        }
        else if (hal_i2c1_bitbang_probe(addr) != 0u)
        {
            g_scan_buf[addr] = 1u;
            count++;
        }
        else
        {
            g_scan_buf[addr] = 0u;
        }
    }
    g_scan_dev_count = count;
}

/*
 * Scan I2C1 bus and print i2cdetect-style table to UART (DEBUG builds only).
 * Also fills the scan buffer so results are readable via I2C.
 */
static void i2c1_scan_and_print(void)
{
    /* Scan to buffer first */
    i2c1_scan_to_buffer();

#ifdef DEBUG_ENABLED
    {
        uint8 addr;
        uint8 col;

        /* Suppress ISR debug, flush ring buffer */
        g_riic_slave_dbg_en = 0u;
        (void)hal_uart_drain(512u);
        { volatile uint32 w = 100000u; while (w-- != 0u) { ; } }
        (void)hal_uart_drain(512u);

        hal_uart_puts("\nI2C1 scan:\n");
        hal_uart_puts("     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f\n");

        for (addr = 0u; addr < 0x78u; addr++)
        {
            col = addr & 0x0Fu;

            if (col == 0u)
            {
                hal_uart_put_hex8(addr);
                hal_uart_putc(':');
            }

            if (addr < 0x03u)
            {
                hal_uart_puts("   ");
            }
            else if (g_scan_buf[addr] != 0u)
            {
                hal_uart_putc(' ');
                hal_uart_put_hex8(addr);
            }
            else
            {
                hal_uart_puts(" --");
            }

            if (col == 0x0Fu)
            {
                hal_uart_puts("\n");
            }
        }
        hal_uart_puts("\n");
        g_riic_slave_dbg_en = g_dbg_i2c_log;
    }
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
    g_pcl_stable = 1u;      /* Default to HIGH (display off) until debounced */
    g_i2c_power_cmd = 1u;   /* Default ON — display turns on when PCL=LOW */
    g_dbg_cmd = DBG_CMD_NONE;
    g_dbg_status = DBG_STATUS_IDLE;
    g_dbg_i2c_log = 1u;    /* I2C slave debug on by default */
    g_scan_dev_count = 0u;
    scan_flush();
    g_bridge_slave = 0u;
    g_bridge_reg = 0u;
    g_bridge_len = 0u;
    g_bridge_cmd = BRIDGE_CMD_NONE;
    g_bridge_status = BRIDGE_STATUS_IDLE;
    {
        uint8 bi;
        for (bi = 0u; bi < BRIDGE_DATA_SIZE; bi++)
        {
            g_bridge_data[bi] = 0u;
        }
    }

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

    /* Check PCL at boot — only power up if LOW.
     * Seed both g_pcl_last and g_pcl_stable so the timer ISR's first
     * tick already considers PCL stable at the boot value, preventing
     * the main loop from racing to power off the display we just
     * powered on. */
    {
        uint8 pcl_boot = hal_gpio_read_ap0(PIN_PCL_BIT);
        g_pcl_last   = pcl_boot;
        g_pcl_stable = pcl_boot;
        g_pcl_debounce = PCL_DEBOUNCE_MS;  /* Already debounced */
    }
    if (g_pcl_stable == 0u)
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
        /* ---- Display power state machine ----
         * g_pcl_stable is published by the timer ISR only after the
         * debounce window completes. Reading it once here gives a
         * race-free snapshot. */
        {
            uint8 want_on;
            want_on = ((g_pcl_stable == 0u) && (g_i2c_power_cmd != 0u))
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
            case DBG_CMD_I2C1_PRINT:
                /* Scan + print to UART (also fills buffer) */
                g_dbg_status = DBG_STATUS_RUNNING;
                i2c1_scan_and_print();
                g_dbg_status = DBG_STATUS_DONE;
                break;
            case DBG_CMD_I2C1_SCAN:
                /* Scan to buffer only (works without UART/DEBUG) */
                g_dbg_status = DBG_STATUS_RUNNING;
                i2c1_scan_to_buffer();
                g_dbg_status = DBG_STATUS_DONE;
                break;
            case DBG_CMD_SCAN_FLUSH:
                /* Clear scan buffer */
                scan_flush();
                break;
            default:
                break;
            }
        }

        /* ---- I2C bridge command handler ---- */
        if (g_bridge_cmd != BRIDGE_CMD_NONE)
        {
            bridge_execute();
            g_bridge_cmd = BRIDGE_CMD_NONE;
        }
    }

    return 0;
}
