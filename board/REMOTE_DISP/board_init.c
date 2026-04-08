/*
 * board_init.c - Power-up sequence for REMOTE_DISP board
 *
 * Translates the BIOS init script (disp-bios-init-script.txt) into
 * bare-metal C. Each power pin is configured as output AND driven high
 * in the same step, matching the BIOS script's incremental approach:
 *   P <port> 20 <mask>     -> set level HIGH
 *   P <port> 82 <mask> 0   -> configure as push-pull output
 *
 * Sequence:
 *   1. Configure GPIO port directions and initial levels
 *   2. Enable main power (5V, 3.3V, PMIC) + 5ms
 *   3. Enable FPGA power rails (1.1V, 1.35V, 1.2V, 2.5V) + 5ms
 *   4. FPGA program + release reset (5ms each)
 *   5. Enable deserializer power + DCDC reset + 5ms
 *   6. SPI chip select init
 *   7. Enable backlight
 *   8. LCD panel reset sequence (with timing)
 */

#include "board.h"
#include "hal_gpio.h"

/* ---- Delay helper ---- */

static void delay_ms(uint32 ms)
{
    /*
     * Busy-wait delay calibrated for 80 MHz CPU clock.
     * Each inner loop iteration is ~4 instructions (dec, compare, branch,
     * plus volatile load/store) = ~5 cycles at 80 MHz = ~62.5 ns.
     * 1 ms = 16000 iterations. Use 20000 for safety margin.
     */
    uint32 outer;
    for (outer = 0u; outer < ms; outer++)
    {
        volatile uint32 d = 20000u;
        while (d-- != 0u)
        {
            ;
        }
    }
}

/* ---- Helper: set pin HIGH and configure as push-pull output ---- */
/*
 * Matches the BIOS script pattern:
 *   P <port> 20 <mask>      -> set level 1
 *   P <port> 82 <mask> 0000 -> configure as push-pull output
 */
static void pin_drive_high(uint8 port, uint8 bit)
{
    hal_gpio_write(port, bit, 1);
    hal_gpio_set_output(port, bit);
}

/* Helper for AP0 pins (analog port, different register set) */
static void ap0_drive_high(uint8 bit)
{
    PORTAPSR0  = PSR_SET(bit);
    PORTAPMSR0 = PSR_CLR(bit);     /* PM=0 -> output */
}

/* ---- GPIO port configuration ---- */

/*
 * Configure all port directions and initial levels.
 * Derived from BIOS script port init section (lines 15-57).
 *
 * BIOS command mapping:
 *   P <port> 10 <mask>       -> clear bits (set level 0)
 *   P <port> 81 <in> <out>   -> set direction (in=input, out=output)
 *   P <port> 82 <out> <od>   -> set output + open-drain
 *   P <port> E0 <mask>       -> set input buffer (Schmitt trigger)
 */

static void port_init(void)
{
    uint8 i;

    /* --- Port AP0 (B0) --- */
    /* BIOS: P B0 10 FE00 / P B0 81 41FF FE00 / P B0 82 FE00 0000 */
    /* Clear output bits, set directions */
    for (i = 0u; i < 16u; i++)
    {
        if ((0xFE00u & ((uint16)1u << i)) != 0u)
        {
            PORTAPSR0  = PSR_CLR(i);
            PORTAPMSR0 = PSR_CLR(i);   /* Output */
        }
    }
    /* Enable input buffer for input pins: 0x41FF */
    PORTAPIBC0 |= 0x41FFu;

    /* --- Port P0 --- */
    /* BIOS: P 00 10 1161 / P 00 81 269E 5961 / P 00 82 5961 0000 / P 00 E0 269E
     * Exclude bits 13,14 (P0_13=UART RX, P0_14=UART TX) — already configured by HAL.
     * 0x1161 & ~0x6000 = 0x1161 (no overlap)
     * 0x5961 & ~0x6000 = 0x1961 (removes bit 14 from output config)
     */
    for (i = 0u; i < 16u; i++)
    {
        if ((0x1161u & ((uint16)1u << i)) != 0u)
        {
            PORTPSR0 = PSR_CLR(i);
        }
    }
    for (i = 0u; i < 16u; i++)
    {
        if ((0x1961u & ((uint16)1u << i)) != 0u)
        {
            PORTPMCSR0 = PSR_CLR(i);
            PORTPMSR0  = PSR_CLR(i);
        }
    }
    /* Input buffer Schmitt trigger for inputs: 0x269E */
    PORTPIBC0 |= 0x269Eu;

    /* --- Port P8 --- */
    /* BIOS: P 08 10 1C23 / P 08 81 E1DC 1E23 / P 08 82 1E23 0000 / P 08 E0 01DC */
    for (i = 0u; i < 16u; i++)
    {
        if ((0x1C23u & ((uint16)1u << i)) != 0u)
        {
            PORTPSR8 = PSR_CLR(i);
        }
    }
    for (i = 0u; i < 16u; i++)
    {
        if ((0x1E23u & ((uint16)1u << i)) != 0u)
        {
            PORTPMCSR8 = PSR_CLR(i);
            PORTPMSR8  = PSR_CLR(i);
        }
    }
    PORTPIBC8 |= 0x01DCu;

    /* --- Port P9 --- */
    /* BIOS: P 09 10 0001 / P 09 81 FF94 006B / P 09 82 006B 0000 / P 09 E0 FF90 */
    PORTPSR9 = PSR_CLR(0);
    for (i = 0u; i < 16u; i++)
    {
        if ((0x006Bu & ((uint16)1u << i)) != 0u)
        {
            PORTPMCSR9 = PSR_CLR(i);
            PORTPMSR9  = PSR_CLR(i);
        }
    }
    PORTPIBC9 |= 0xFF90u;

    /* --- Port P10 --- */
    /* BIOS: P 10 10 94B3 / P 10 81 4B4C B4B3 / P 10 82 B4B3 0000 / P 10 E0 4B40 */
    for (i = 0u; i < 16u; i++)
    {
        if ((0x94B3u & ((uint16)1u << i)) != 0u)
        {
            PORTPSR10 = PSR_CLR(i);
        }
    }
    for (i = 0u; i < 16u; i++)
    {
        if ((0xB4B3u & ((uint16)1u << i)) != 0u)
        {
            PORTPMCSR10 = PSR_CLR(i);
            PORTPMSR10  = PSR_CLR(i);
        }
    }
    PORTPIBC10 |= 0x4B40u;

    /* --- Port P11 --- */
    /* LCD_PON (P11_0): output, initially LOW */
    PORTPSR11   = PSR_CLR(PIN_LCD_PON_BIT);
    PORTPMCSR11 = PSR_CLR(PIN_LCD_PON_BIT);
    PORTPMSR11  = PSR_CLR(PIN_LCD_PON_BIT);

    delay_ms(1);
}

/* ---- Power-up sequence ---- */

static void power_main_enable(void)
{
    /* BIOS lines 63-72: set HIGH then configure as output */

    /* IOC_ON_UG5V (P9_3) -> HIGH */
    pin_drive_high(PIN_IOC_ON_UG5V_PORT, PIN_IOC_ON_UG5V_BIT);

    /* EN 3V3_SW (P9_5) -> HIGH */
    pin_drive_high(PIN_EN_3V3_SW_PORT, PIN_EN_3V3_SW_BIT);

    /* RTQ6749_EN (P10_4) -> HIGH */
    pin_drive_high(PIN_RTQ6749_EN_PORT, PIN_RTQ6749_EN_BIT);

    delay_ms(5);
}

static void power_fpga_enable(void)
{
    /* BIOS lines 75-90 */

    /* IOC_ON_UG1V1 (P10_9) -> HIGH */
    pin_drive_high(PIN_IOC_ON_UG1V1_PORT, PIN_IOC_ON_UG1V1_BIT);

    /* IOC_ON_UG1V35 (P10_10) -> HIGH */
    pin_drive_high(PIN_IOC_ON_UG1V35_PORT, PIN_IOC_ON_UG1V35_BIT);

    /* IOC_ON_UG1V2 (P10_12) -> HIGH */
    pin_drive_high(PIN_IOC_ON_UG1V2_PORT, PIN_IOC_ON_UG1V2_BIT);

    /* IOC_ON_UG2V5 (P10_7) -> HIGH */
    pin_drive_high(PIN_IOC_ON_UG2V5_PORT, PIN_IOC_ON_UG2V5_BIT);

    delay_ms(5);
}

static void fpga_program_and_reset(void)
{
    /* BIOS lines 92-100 */

    /* PROGRAM (P8_6) -> HIGH */
    pin_drive_high(PIN_FPGA_PROGRAM_PORT, PIN_FPGA_PROGRAM_BIT);
    delay_ms(5);

    /* FPGA_RSTN (P8_5) -> HIGH (release reset) */
    pin_drive_high(PIN_FPGA_RSTN_PORT, PIN_FPGA_RSTN_BIT);
    delay_ms(5);
}

static void deser_power_enable(void)
{
    /* BIOS lines 102-118 */

    /* IOC_ON_UG1V8 (AP0_5) -> HIGH + output */
    ap0_drive_high(PIN_IOC_ON_UG1V8_BIT);

    /* IOC_ON_UG1V15 (AP0_6) -> HIGH + output */
    ap0_drive_high(PIN_IOC_ON_UG1V15_BIT);

    /* DCDC_RST (P8_8) -> HIGH */
    pin_drive_high(PIN_DCDC_RST_PORT, PIN_DCDC_RST_BIT);
    delay_ms(5);

    /* WP (AP0_14) -> HIGH (release write-protect) */
    ap0_drive_high(PIN_DESER_WP_BIT);
}

static void spi_cs_init(void)
{
    /* BIOS lines 146-147: SPI_CS (P0_11) as push-pull output, LOW */
    hal_gpio_write(PIN_SPI_CS_PORT, PIN_SPI_CS_BIT, 0);
    hal_gpio_set_output(PIN_SPI_CS_PORT, PIN_SPI_CS_BIT);
}

static void backlight_enable(void)
{
    /* BIOS lines 157-158 */

    /* VLED_ON (P10_11) -> HIGH */
    pin_drive_high(PIN_VLED_ON_PORT, PIN_VLED_ON_BIT);
}

static void lcd_reset_sequence(void)
{
    /* BIOS lines 166-186 */

    /* LCD_TP_RST (P0_10) -> LOW, configure as output */
    hal_gpio_write(PIN_LCD_TP_RST_PORT, PIN_LCD_TP_RST_BIT, 0);
    hal_gpio_set_output(PIN_LCD_TP_RST_PORT, PIN_LCD_TP_RST_BIT);
    delay_ms(10);

    /* LCD_RST (P10_15) -> LOW */
    hal_gpio_write(PIN_LCD_RST_PORT, PIN_LCD_RST_BIT, 0);
    hal_gpio_set_output(PIN_LCD_RST_PORT, PIN_LCD_RST_BIT);

    /* LCD_PON (P11_0) -> LOW (already output from port_init) */
    hal_gpio_write(PIN_LCD_PON_PORT, PIN_LCD_PON_BIT, 0);
    delay_ms(6);

    /* LCD_RST (P10_15) -> HIGH */
    hal_gpio_write(PIN_LCD_RST_PORT, PIN_LCD_RST_BIT, 1);
    delay_ms(5);

    /* LCD_TP_RST (P0_10) -> HIGH */
    hal_gpio_write(PIN_LCD_TP_RST_PORT, PIN_LCD_TP_RST_BIT, 1);
    delay_ms(95);

    /* LCD_PON (P11_0) -> HIGH */
    hal_gpio_write(PIN_LCD_PON_PORT, PIN_LCD_PON_BIT, 1);
}

/* ---- Power-down helpers (reverse of power-up) ---- */

static void lcd_shutdown(void)
{
    /* Reverse of lcd_reset_sequence: assert resets, power off */
    hal_gpio_write(PIN_LCD_PON_PORT, PIN_LCD_PON_BIT, 0);
    hal_gpio_write(PIN_LCD_TP_RST_PORT, PIN_LCD_TP_RST_BIT, 0);
    delay_ms(5);
    hal_gpio_write(PIN_LCD_RST_PORT, PIN_LCD_RST_BIT, 0);
    delay_ms(5);
}

static void backlight_disable(void)
{
    hal_gpio_write(PIN_VLED_ON_PORT, PIN_VLED_ON_BIT, 0);
}

static void deser_power_disable(void)
{
    /* WP assert (write-protect) */
    PORTAPSR0 = PSR_CLR(PIN_DESER_WP_BIT);
    delay_ms(1);

    /* DCDC_RST (P8_8) -> LOW */
    hal_gpio_write(PIN_DCDC_RST_PORT, PIN_DCDC_RST_BIT, 0);

    /* IOC_ON_UG1V15 (AP0_6) -> LOW */
    PORTAPSR0 = PSR_CLR(PIN_IOC_ON_UG1V15_BIT);

    /* IOC_ON_UG1V8 (AP0_5) -> LOW */
    PORTAPSR0 = PSR_CLR(PIN_IOC_ON_UG1V8_BIT);
    delay_ms(5);
}

static void fpga_shutdown(void)
{
    /* Assert FPGA reset, then deassert program */
    hal_gpio_write(PIN_FPGA_RSTN_PORT, PIN_FPGA_RSTN_BIT, 0);
    delay_ms(1);
    hal_gpio_write(PIN_FPGA_PROGRAM_PORT, PIN_FPGA_PROGRAM_BIT, 0);
    delay_ms(5);
}

static void power_fpga_disable(void)
{
    hal_gpio_write(PIN_IOC_ON_UG2V5_PORT, PIN_IOC_ON_UG2V5_BIT, 0);
    hal_gpio_write(PIN_IOC_ON_UG1V2_PORT, PIN_IOC_ON_UG1V2_BIT, 0);
    hal_gpio_write(PIN_IOC_ON_UG1V35_PORT, PIN_IOC_ON_UG1V35_BIT, 0);
    hal_gpio_write(PIN_IOC_ON_UG1V1_PORT, PIN_IOC_ON_UG1V1_BIT, 0);
    delay_ms(5);
}

static void power_main_disable(void)
{
    hal_gpio_write(PIN_RTQ6749_EN_PORT, PIN_RTQ6749_EN_BIT, 0);
    hal_gpio_write(PIN_EN_3V3_SW_PORT, PIN_EN_3V3_SW_BIT, 0);
    hal_gpio_write(PIN_IOC_ON_UG5V_PORT, PIN_IOC_ON_UG5V_BIT, 0);
    delay_ms(5);
}

/* ---- Public API ---- */

void board_init(void)
{
    /* 1. Configure GPIO port directions and initial levels */
    port_init();

    /* 2. Main power supplies */
    power_main_enable();

    /* 3. FPGA power rails */
    power_fpga_enable();

    /* 4. FPGA program + release reset */
    fpga_program_and_reset();

    /* 5. Deserializer power + DCDC + write-protect release */
    deser_power_enable();

    /* 6. SPI chip select init */
    spi_cs_init();

    /* 7. Backlight enable */
    backlight_enable();

    /* 8. LCD panel reset sequence */
    lcd_reset_sequence();
}

void board_power_down(void)
{
    /*
     * Reverse order of board_init steps 8..3.
     * Main power (step 2: 5V, 3.3V, PMIC) is NOT disabled —
     * the MCU runs on these rails and must stay powered to
     * monitor PCL for wake-up.
     */

    /* 8. LCD shutdown (assert resets, power off) */
    lcd_shutdown();

    /* 7. Backlight disable */
    backlight_disable();

    /* 5. Deserializer power disable */
    deser_power_disable();

    /* 4. FPGA reset + program deassert */
    fpga_shutdown();

    /* 3. FPGA power rails disable */
    power_fpga_disable();

    /* Note: step 2 (power_main_disable) intentionally skipped */
}
