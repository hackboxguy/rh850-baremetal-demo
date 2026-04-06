/*
 * hal_i2c_bitbang.h - Bit-banged I2C master driver (GPIO-based)
 */

#ifndef HAL_I2C_BITBANG_H
#define HAL_I2C_BITBANG_H

#include "dr7f701686.dvf.h"

/*
 * Initialize bit-banged I2C on board I2C pins (P10_2 SDA, P10_3 SCL).
 * Sets pins as GPIO with input buffer enabled.
 * Generates a START+STOP sequence to reset any stuck slave.
 * Does NOT require PLL (works at any clock speed).
 */
void hal_i2c_bitbang_init(void);

/*
 * Write data bytes to an I2C slave using bit-bang.
 * Returns 1 if slave ACKed the address, 0 if NACK.
 */
uint8 hal_i2c_bitbang_write(uint8 addr_7bit, const uint8 *data, uint8 len);

/*
 * Read data bytes from an I2C slave using bit-bang.
 * Returns 1 if slave ACKed the address, 0 if NACK.
 */
uint8 hal_i2c_bitbang_read(uint8 addr_7bit, uint8 *data, uint8 len);

#endif /* HAL_I2C_BITBANG_H */
