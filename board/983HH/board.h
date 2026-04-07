/*
 * board.h - Board configuration for 983HH
 *
 * Target: R7F7016863 (RH850/F1KM-S1, 100-pin LQFP)
 * Board:  983HH
 */

#ifndef BOARD_983HH_H
#define BOARD_983HH_H

/* ---- Oscillator ---- */
#define BOARD_MAIN_OSC_HZ       16000000u   /* 16 MHz crystal on X1/X2 */
#define BOARD_HAS_PLL0          0           /* F1KM-S1: no PLL0 */
#define BOARD_HAS_PLL1          1
#define BOARD_PLL1C_VALUE       0x00010B3Bu /* 16 MHz -> 480 MHz VCO -> 80 MHz PPLLCLK */
#define BOARD_CPU_HZ            80000000u   /* CPUCLK after PLL */
#define BOARD_PCLK_HZ           40000000u   /* PPLLCLK2 (peripheral bus) */
#define BOARD_PCLK_NOPLL_HZ     4000000u    /* CPUCLK2 before PLL (HS IntOSC/2) */

/* ---- UART debug (RLIN32) ---- */
#define BOARD_UART_CHANNEL      2           /* RLIN32 */
#define BOARD_UART_TX_PORT      0
#define BOARD_UART_TX_BIT       14
#define BOARD_UART_RX_PORT      0
#define BOARD_UART_RX_BIT       13
#define BOARD_UART_BAUD         115200u
#define BOARD_UART_AF           1           /* AF1 */

/* ---- I2C (RIIC0 / bit-bang) ---- */
#define BOARD_I2C_SDA_PORT      10
#define BOARD_I2C_SDA_BIT       2
#define BOARD_I2C_SCL_PORT      10
#define BOARD_I2C_SCL_BIT       3
#define BOARD_I2C_AF            2           /* AF2 for RIIC0 */
#define BOARD_I2C_SLAVE_ADDR    0x50u       /* Default slave address */

/* ---- LED ---- */
#define BOARD_LED_PORT          9
#define BOARD_LED_BIT           6

/* ---- DIP switches (AP0_7 through AP0_14) ---- */
#define BOARD_DIP_PORT_ANALOG   0           /* AP0 */
#define BOARD_DIP_START_BIT     7
#define BOARD_DIP_COUNT         8

/* ---- Board init ---- */
void board_init(void);

#endif /* BOARD_983HH_H */
