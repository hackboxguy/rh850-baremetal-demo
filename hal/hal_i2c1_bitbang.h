/*
 * hal_i2c1_bitbang.h - Bit-banged I2C master on bus 1 (P8_0 SDA, P8_1 SCL)
 *
 * Software I2C for the second bus. Used for deserializer, touch panel,
 * and bus scanning on REMOTE_DISP board.
 */

#ifndef HAL_I2C1_BITBANG_H
#define HAL_I2C1_BITBANG_H

#include "dr7f701686.dvf.h"

/* Initialize bit-bang I2C1 on P8_0 (SDA) / P8_1 (SCL).
 * Sets pins as GPIO, enables input buffer, runs bus recovery. */
void hal_i2c1_bitbang_init(void);

/* Probe address: returns 1 if ACK, 0 if NACK. */
uint8 hal_i2c1_bitbang_probe(uint8 addr_7bit);

/* Write data to slave. Returns 1 on ACK, 0 on NACK. */
uint8 hal_i2c1_bitbang_write(uint8 addr_7bit, const uint8 *data, uint8 len);

/* Read data from slave. Returns 1 on ACK, 0 on NACK. */
uint8 hal_i2c1_bitbang_read(uint8 addr_7bit, uint8 *data, uint8 len);

#endif /* HAL_I2C1_BITBANG_H */
