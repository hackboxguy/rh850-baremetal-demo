/*
 * hal_riic_master.h - RIIC0 hardware I2C master driver (polling)
 */

#ifndef HAL_RIIC_MASTER_H
#define HAL_RIIC_MASTER_H

#include "dr7f701686.dvf.h"

/*
 * Initialize RIIC0 as I2C master (~94 kHz SCL).
 * Configures pins (P10_2 SDA, P10_3 SCL, AF2) and RIIC peripheral.
 * Requires PLL running (PPLLCLK2 = 40 MHz).
 */
void hal_riic_master_init(void);

/*
 * Write data bytes to an I2C slave (polling).
 * Returns 1 on success (all ACKed), 0 on NACK or timeout.
 */
uint8 hal_riic_master_write(uint8 addr_7bit, const uint8 *data, uint8 len);

/*
 * Read data bytes from an I2C slave (polling).
 * Returns 1 on success, 0 on NACK or timeout.
 */
uint8 hal_riic_master_read(uint8 addr_7bit, uint8 *data, uint8 len);

#endif /* HAL_RIIC_MASTER_H */
