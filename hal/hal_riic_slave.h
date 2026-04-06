/*
 * hal_riic_slave.h - RIIC0 hardware I2C slave driver (interrupt-driven)
 *
 * 16-bit sub-addressing (EEPROM-style, 24C256/24C512 compatible):
 *   Write: [slave+W] [addr_hi] [addr_lo] [data0] [data1] ...
 *   Read:  [slave+W] [addr_hi] [addr_lo] [slave+R] [data0] ...
 *   Current-address read: [slave+R] [data0] ... (continues from last addr)
 *
 * Address auto-increments after each byte and wraps at 0xFFFF -> 0x0000.
 */

#ifndef HAL_RIIC_SLAVE_H
#define HAL_RIIC_SLAVE_H

#include "dr7f701686.dvf.h"

/* Application callbacks for I2C slave register access (16-bit address) */
typedef void    (*hal_riic_slave_write_cb)(uint16 reg_addr, uint8 value);
typedef uint8   (*hal_riic_slave_read_cb)(uint16 reg_addr);

/*
 * Initialize RIIC0 as I2C slave.
 * Configures pins (P10_2 SDA, P10_3 SCL, AF2), RIIC peripheral,
 * and INTC2 interrupts (ch76-79).
 *
 * Requires PLL running (PPLLCLK2 = 40 MHz).
 * Call __EI() after this to enable global interrupts.
 */
void hal_riic_slave_init(uint8 slave_addr,
                         hal_riic_slave_write_cb on_write,
                         hal_riic_slave_read_cb  on_read);

/* ISR handlers (called from EIINTTBL in boot.asm, do not call directly) */
void hal_riic0_isr_ti(void);    /* ch76: Transmit data empty */
void hal_riic0_isr_ee(void);    /* ch77: Error/event */
void hal_riic0_isr_ri(void);    /* ch78: Receive complete */
void hal_riic0_isr_tei(void);   /* ch79: Transmit end */

#endif /* HAL_RIIC_SLAVE_H */
