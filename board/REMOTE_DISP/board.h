/*
 * board.h - Board configuration for REMOTE_DISP
 *
 * Target: R7F7016863 (RH850/F1KM-S1, 100-pin LQFP)
 * Board:  Remote Display (15.6" POC_9090 panel)
 *         FPGA + FPD-Link deserializer + LCD + touch panel
 */

#ifndef BOARD_REMOTE_DISP_H
#define BOARD_REMOTE_DISP_H

#define BOARD_NAME              "REMOTE_DISP"

/* ---- Oscillator ---- */
#define BOARD_MAIN_OSC_HZ       16000000u
#define BOARD_HAS_PLL0          0
#define BOARD_HAS_PLL1          1
#define BOARD_PLL1C_VALUE       0x00010B3Bu
#define BOARD_CPU_HZ            80000000u
#define BOARD_PCLK_HZ           40000000u
#define BOARD_PCLK_NOPLL_HZ    4000000u

/* ---- UART debug (RLIN32) ---- */
#define BOARD_UART_CHANNEL      2
#define BOARD_UART_TX_PORT      0
#define BOARD_UART_TX_BIT       14
#define BOARD_UART_RX_PORT      0
#define BOARD_UART_RX_BIT       13
#define BOARD_UART_BAUD         115200u
#define BOARD_UART_AF           1

/* ---- I2C0 slave (RIIC0) ---- */
#define BOARD_I2C_SDA_PORT      10
#define BOARD_I2C_SDA_BIT       2
#define BOARD_I2C_SCL_PORT      10
#define BOARD_I2C_SCL_BIT       3
#define BOARD_I2C_AF            2
#define BOARD_I2C_SLAVE_ADDR    0x50u
/* 400 kHz fast mode: CKS=/2 (20 MHz ref), BRL for SCL sync */
#define BOARD_I2C_MR1           0x10u       /* CKS=/2 */
#define BOARD_I2C_BRL           0xF2u       /* 0xE0 | 0x12 (18 cycles low) */

/* ---- Backlight NTC temperature sensor (AP0_0 = ANI000) ---- */
#define BOARD_NTC_ADC_CHANNEL   0           /* ADCA0 physical channel ANI00 */
/*
 * NTC parameters: 10K ohm at 25C, Beta=3950 (B25/85).
 * Assumes voltage divider: Vcc --- [R_pullup] --- ADC --- [NTC] --- GND
 * ADC reference = 3.3V, 12-bit (0-4095).
 * Update BOARD_NTC_BETA when exact NTC datasheet is available.
 */
#define BOARD_NTC_BETA          3950u       /* Beta value (K) */
#define BOARD_NTC_R25           10000u      /* Resistance at 25C (ohms) */
#define BOARD_NTC_R_PULLUP      10000u      /* Pull-up resistor (ohms) */
#define BOARD_NTC_T25_K         29815u      /* 25C in Kelvin x100 (298.15K) */

/* ---- Power control pins ---- */

/* Main power */
#define PIN_IOC_ON_UG5V_PORT    9       /* P9_3: Main 5V supply */
#define PIN_IOC_ON_UG5V_BIT     3
#define PIN_EN_3V3_SW_PORT      9       /* P9_5: 3.3V switched supply */
#define PIN_EN_3V3_SW_BIT       5
#define PIN_RTQ6749_EN_PORT     10      /* P10_4: PMIC enable */
#define PIN_RTQ6749_EN_BIT      4

/* FPGA power rails */
#define PIN_IOC_ON_UG1V1_PORT   10      /* P10_9: FPGA 1.1V */
#define PIN_IOC_ON_UG1V1_BIT    9
#define PIN_IOC_ON_UG1V35_PORT  10      /* P10_10: FPGA 1.35V */
#define PIN_IOC_ON_UG1V35_BIT   10
#define PIN_IOC_ON_UG1V2_PORT   10      /* P10_12: FPGA 1.2V */
#define PIN_IOC_ON_UG1V2_BIT    12
#define PIN_IOC_ON_UG2V5_PORT   10      /* P10_7: FPGA 2.5V */
#define PIN_IOC_ON_UG2V5_BIT    7

/* FPGA control */
#define PIN_FPGA_PROGRAM_PORT   8       /* P8_6: FPGA programming */
#define PIN_FPGA_PROGRAM_BIT    6
#define PIN_FPGA_RSTN_PORT      8       /* P8_5: FPGA reset (active low) */
#define PIN_FPGA_RSTN_BIT       5

/* Deserializer power & control */
#define PIN_IOC_ON_UG1V8_PORT   0xB0u   /* AP0_5: Deser 1.8V (analog port) */
#define PIN_IOC_ON_UG1V8_BIT    5
#define PIN_IOC_ON_UG1V15_PORT  0xB0u   /* AP0_6: Deser 1.15V (analog port) */
#define PIN_IOC_ON_UG1V15_BIT   6
#define PIN_DCDC_RST_PORT       8       /* P8_8: Deser DCDC reset */
#define PIN_DCDC_RST_BIT        8
#define PIN_DESER_WP_PORT       0xB0u   /* AP0_14: Deser write-protect */
#define PIN_DESER_WP_BIT        14

/* LCD panel control */
#define PIN_LCD_TP_RST_PORT     0       /* P0_10: Touch panel reset */
#define PIN_LCD_TP_RST_BIT      10
#define PIN_LCD_RST_PORT        10      /* P10_15: LCD reset */
#define PIN_LCD_RST_BIT         15
#define PIN_LCD_PON_PORT        11      /* P11_0: LCD power-on */
#define PIN_LCD_PON_BIT         0

/* Backlight */
#define PIN_VLED_ON_PORT        10      /* P10_11: Backlight enable */
#define PIN_VLED_ON_BIT         11

/* SPI chip select */
#define PIN_SPI_CS_PORT         0       /* P0_11: SPI chip select */
#define PIN_SPI_CS_BIT          11

/* ---- Board init ---- */
void board_init(void);

#endif /* BOARD_REMOTE_DISP_H */
