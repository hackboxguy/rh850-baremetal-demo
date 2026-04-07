/*
 * board_init.c - Power-up sequence for REMOTE_DISP board
 *
 * Translates the BIOS init script (disp-bios-init-script.txt) into
 * bare-metal C. Initializes GPIO ports, power rails, FPGA, deserializer,
 * LCD panel, and backlight in the correct order with required delays.
 *
 * Sequence:
 *   1. Configure GPIO port directions
 *   2. Enable main power (5V, 3.3V, PMIC)
 *   3. Enable FPGA power rails (1.1V, 1.35V, 1.2V, 2.5V)
 *   4. FPGA program + release reset
 *   5. Enable deserializer power + release reset
 *   6. Enable backlight
 *   7. LCD panel reset sequence (with timing)
 */

#include "board.h"
#include "hal_gpio.h"

/* ---- Delay helper ---- */

static void delay_ms(uint32 ms)
{
    /* ~80000 iterations per ms at 80 MHz CPU (rough estimate).
     * For precise timing, use a hardware timer. */
    volatile uint32 d = ms * 80000u;
    while (d-- != 0u)
    {
        ;
    }
}

/* ---- GPIO port configuration ---- */

/*
 * Configure all port directions and initial levels.
 * Derived from BIOS script port init section.
 *
 * BIOS command mapping:
 *   P <port> 10 <mask>       -> clear bits (set level 0)
 *   P <port> 20 <mask>       -> set bits (set level 1)
 *   P <port> 81 <in> <out>   -> set direction (in=input mask, out=output mask)
 *   P <port> 82 <out> <od>   -> set output + open-drain
 *   P <port> E0 <mask>       -> set input buffer (Schmitt trigger)
 *
 * In bare-metal:
 *   Level 0/1:   PSR register (atomic set/reset)
 *   Direction:   PMSR register (0=output, 1=input) via atomic set/reset
 *   GPIO mode:   PMCSR register (clear bit = GPIO mode)
 *   Input buf:   PIBC register (enable Schmitt trigger)
 */

static void port_init(void)
{
    uint8 i;

    /* --- Port AP0 (B0) --- */
    /* Clear AP0 output bits: 0xFE00 = bits 9-15 */
    /* Inputs: 0x41FF, Outputs: 0xFE00 */
    /* Note: AP0 uses APMSR0/APSR0 registers */
    PORTAPSR0 &= ~(uint32)0xFE00u;

    /* --- Port P0 --- */
    /* Clear bits: 0x1161 = bits 0,5,6,8,12 */
    for (i = 0u; i < 16u; i++)
    {
        if ((0x1161u & ((uint16)1u << i)) != 0u)
        {
            PORTPSR0 = PSR_CLR(i);
        }
    }
    /* Outputs: 0x5961 = bits 0,5,6,8,10,11,12,14 */
    for (i = 0u; i < 16u; i++)
    {
        if ((0x5961u & ((uint16)1u << i)) != 0u)
        {
            PORTPMCSR0 = PSR_CLR(i);   /* GPIO mode */
            PORTPMSR0  = PSR_CLR(i);   /* Output */
        }
    }

    /* --- Port P8 --- */
    /* Clear bits: 0x1C23 = bits 0,1,5,10,11,12 */
    for (i = 0u; i < 16u; i++)
    {
        if ((0x1C23u & ((uint16)1u << i)) != 0u)
        {
            PORTPSR8 = PSR_CLR(i);
        }
    }
    /* Outputs: 0x1E23 = bits 0,1,5,9,10,11,12 */
    for (i = 0u; i < 16u; i++)
    {
        if ((0x1E23u & ((uint16)1u << i)) != 0u)
        {
            PORTPMCSR8 = PSR_CLR(i);
            PORTPMSR8  = PSR_CLR(i);
        }
    }

    /* --- Port P9 --- */
    /* Clear bits: 0x0001 = bit 0 */
    PORTPSR9 = PSR_CLR(0);
    /* Outputs: 0x006B = bits 0,1,3,5,6 */
    for (i = 0u; i < 16u; i++)
    {
        if ((0x006Bu & ((uint16)1u << i)) != 0u)
        {
            PORTPMCSR9 = PSR_CLR(i);
            PORTPMSR9  = PSR_CLR(i);
        }
    }

    /* --- Port P10 --- */
    /* Clear bits: 0x94B3 = bits 0,1,4,5,7,10,12,15 */
    for (i = 0u; i < 16u; i++)
    {
        if ((0x94B3u & ((uint16)1u << i)) != 0u)
        {
            PORTPSR10 = PSR_CLR(i);
        }
    }
    /* Outputs: 0xB4B3 = bits 0,1,4,5,7,8,10,12,13,15 */
    for (i = 0u; i < 16u; i++)
    {
        if ((0xB4B3u & ((uint16)1u << i)) != 0u)
        {
            PORTPMCSR10 = PSR_CLR(i);
            PORTPMSR10  = PSR_CLR(i);
        }
    }

    /* --- Port P11 --- */
    /* P11_0 as output (LCD_PON) */
    PORTPMCSR11 = PSR_CLR(PIN_LCD_PON_BIT);
    PORTPMSR11  = PSR_CLR(PIN_LCD_PON_BIT);

    delay_ms(1);
}

/* ---- Power-up sequence ---- */

static void power_main_enable(void)
{
    /* IOC_ON_UG5V (P9_3) -> HIGH */
    hal_gpio_write(PIN_IOC_ON_UG5V_PORT, PIN_IOC_ON_UG5V_BIT, 1);

    /* EN 3V3_SW (P9_5) -> HIGH */
    hal_gpio_write(PIN_EN_3V3_SW_PORT, PIN_EN_3V3_SW_BIT, 1);

    /* RTQ6749_EN (P10_4) -> HIGH */
    hal_gpio_write(PIN_RTQ6749_EN_PORT, PIN_RTQ6749_EN_BIT, 1);

    delay_ms(5);
}

static void power_fpga_enable(void)
{
    /* IOC_ON_UG1V1 (P10_9) -> HIGH */
    hal_gpio_write(PIN_IOC_ON_UG1V1_PORT, PIN_IOC_ON_UG1V1_BIT, 1);

    /* IOC_ON_UG1V35 (P10_10) -> HIGH */
    hal_gpio_write(PIN_IOC_ON_UG1V35_PORT, PIN_IOC_ON_UG1V35_BIT, 1);

    /* IOC_ON_UG1V2 (P10_12) -> HIGH */
    hal_gpio_write(PIN_IOC_ON_UG1V2_PORT, PIN_IOC_ON_UG1V2_BIT, 1);

    /* IOC_ON_UG2V5 (P10_7) -> HIGH */
    hal_gpio_write(PIN_IOC_ON_UG2V5_PORT, PIN_IOC_ON_UG2V5_BIT, 1);

    delay_ms(5);
}

static void fpga_program_and_reset(void)
{
    /* PROGRAM (P8_6) -> HIGH */
    hal_gpio_write(PIN_FPGA_PROGRAM_PORT, PIN_FPGA_PROGRAM_BIT, 1);
    delay_ms(5);

    /* FPGA_RSTN (P8_5) -> HIGH (release reset) */
    hal_gpio_write(PIN_FPGA_RSTN_PORT, PIN_FPGA_RSTN_BIT, 1);
    delay_ms(5);
}

static void deser_power_enable(void)
{
    /* IOC_ON_UG1V8 (AP0_5) -> HIGH */
    PORTAPSR0 = PSR_SET(PIN_IOC_ON_UG1V8_BIT);

    /* IOC_ON_UG1V15 (AP0_6) -> HIGH */
    PORTAPSR0 = PSR_SET(PIN_IOC_ON_UG1V15_BIT);

    /* DCDC_RST (P8_8) -> HIGH */
    hal_gpio_write(PIN_DCDC_RST_PORT, PIN_DCDC_RST_BIT, 1);
    delay_ms(5);

    /* WP (AP0_14) -> HIGH (release write-protect) */
    PORTAPSR0 = PSR_SET(PIN_DESER_WP_BIT);
}

static void backlight_enable(void)
{
    /* VLED_ON (P10_11) -> HIGH */
    hal_gpio_write(PIN_VLED_ON_PORT, PIN_VLED_ON_BIT, 1);
}

static void lcd_reset_sequence(void)
{
    /* LCD_TP_RST (P0_10) -> LOW */
    hal_gpio_write(PIN_LCD_TP_RST_PORT, PIN_LCD_TP_RST_BIT, 0);
    delay_ms(10);

    /* LCD_RST (P10_15) -> LOW */
    hal_gpio_write(PIN_LCD_RST_PORT, PIN_LCD_RST_BIT, 0);

    /* LCD_PON (P11_0) -> LOW */
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

    /* 6. Backlight enable */
    backlight_enable();

    /* 7. LCD panel reset sequence */
    lcd_reset_sequence();
}
