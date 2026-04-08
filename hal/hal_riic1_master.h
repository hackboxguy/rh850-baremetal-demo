/*
 * hal_riic1_master.h - RIIC1 hardware I2C master driver (polling)
 *
 * Second I2C bus on P8_0 (SDA1), P8_1 (SCL1).
 * Used for deserializer, touch panel, and bus scanning.
 */

#ifndef HAL_RIIC1_MASTER_H
#define HAL_RIIC1_MASTER_H

#include "dr7f701686.dvf.h"

/*
 * Initialize RIIC1 as I2C master (~100 kHz SCL).
 * Configures pins P8_0 (SDA1), P8_1 (SCL1) with open-drain + PBDC.
 * Requires PLL running (PPLLCLK2 = 40 MHz).
 */
void hal_riic1_master_init(void);

/*
 * Probe an I2C address (send START + addr + W, check ACK, send STOP).
 * Returns 1 if device ACKs, 0 if NACK or timeout.
 */
uint8 hal_riic1_master_probe(uint8 addr_7bit);

/*
 * Write data bytes to an I2C slave on bus 1 (polling).
 * Returns 1 on success (all ACKed), 0 on NACK or timeout.
 */
uint8 hal_riic1_master_write(uint8 addr_7bit, const uint8 *data, uint8 len);

/*
 * Read data bytes from an I2C slave on bus 1 (polling).
 * Returns 1 on success, 0 on NACK or timeout.
 */
uint8 hal_riic1_master_read(uint8 addr_7bit, uint8 *data, uint8 len);

#endif /* HAL_RIIC1_MASTER_H */
