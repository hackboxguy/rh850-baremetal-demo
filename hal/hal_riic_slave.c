/*
 * hal_riic_slave.c - RIIC0 hardware I2C slave driver (interrupt-driven)
 *
 * 16-bit sub-addressing (EEPROM-style, 24C256/24C512 compatible):
 *   Write: [slave+W] [addr_hi] [addr_lo] [data0] [data1] ...
 *   Read:  [slave+W] [addr_hi] [addr_lo] [slave+R] [data0] ...
 *   Current-address read: [slave+R] [data0] ... (uses last set addr)
 *
 * Address auto-increments after each byte and wraps at 0xFFFF -> 0x0000.
 *
 * Pin config: P10_2 (SDA), P10_3 (SCL), AF2 with open-drain + PBDC.
 * Proven working on 983HH board with PLL at 40 MHz PPLLCLK2.
 *
 * Key learnings from bringup:
 *   - PBDC=1 is critical for RIIC to read back SDA/SCL states
 *   - PODC=1 required for I2C open-drain (protected write via PPCMD10)
 *   - IER=0xFC: SR2 flags only set when IER bits are enabled, even polled
 *   - AAS0 flag stays set across RI interrupts; must clear after first RI
 *   - STOP+RDRF race: EE ISR must not reset state if RDRF pending
 */

#include "hal_riic_slave.h"
#include "hal_gpio.h"
#include "board.h"
#include "board_vectors.h"
#include "lib_debug.h"

/* INTC2 interrupt control registers */
#define ICRIIC0TI   (*(volatile uint16 *)ICR_RIIC0_TI_ADDR)
#define ICRIIC0EE   (*(volatile uint16 *)ICR_RIIC0_EE_ADDR)
#define ICRIIC0RI   (*(volatile uint16 *)ICR_RIIC0_RI_ADDR)
#define ICRIIC0TEI  (*(volatile uint16 *)ICR_RIIC0_TEI_ADDR)

/* INTC2 bit definitions */
#define ICR_MK      ((uint16)1u << 7)       /* Interrupt mask */
#define ICR_TB      ((uint16)1u << 6)       /* Table reference */
#define ICR_RF      ((uint16)1u << 12)      /* Request flag */

/* RIIC SR2 bit definitions */
#define SR2_TDRE    0x80u
#define SR2_TEND    0x40u
#define SR2_RDRF    0x20u
#define SR2_NACKF   0x10u
#define SR2_STOP    0x08u
#define SR2_START   0x04u
#define SR2_AL      0x02u

/*
 * Slave state machine (16-bit address):
 *
 *   IDLE -> (address match, master WRITE) -> ADDR_HI
 *        -> (address match, master READ)  -> SENDING_DATA (current-addr read)
 *
 *   ADDR_HI -> (receive addr_hi byte) -> ADDR_LO
 *   ADDR_LO -> (receive addr_lo byte) -> RECEIVING_DATA
 *
 *   RECEIVING_DATA -> (receive data bytes, auto-increment, wrap at 0xFFFF)
 *   SENDING_DATA   -> (transmit data bytes via TI ISR, auto-increment)
 */
#define ST_IDLE             0u
#define ST_ADDR_HI          1u
#define ST_ADDR_LO          2u
#define ST_RECEIVING_DATA   3u
#define ST_SENDING_DATA     4u
#define ST_SEND_DONE        5u

static volatile uint8  g_slave_state;
static volatile uint16 g_reg_addr;      /* 16-bit register address */

static hal_riic_slave_write_cb g_on_write;
static hal_riic_slave_read_cb  g_on_read;

/* ---- Pin configuration ---- */

static void riic0_pin_setup(uint8 bit)
{
    uint32 tmp;

    /* Clear input buffer and bidirectional first */
    PORTPIBC10  &= ~((uint16)1u << bit);
    PORT.PBDC10 &= ~((uint16)1u << bit);
    PORTPM10    |= ((uint16)1u << bit);       /* Input initially */
    PORTPMC10   &= ~((uint16)1u << bit);      /* GPIO initially */
    PORTPIPC10  &= ~((uint16)1u << bit);      /* No PIPC */

    /* Clear PDSC (drive strength) - protected write */
    tmp = PORT.PDSC10;
    PORTPPCMD10 = 0xA5u;
    PORT.PDSC10 = (tmp & ~((uint32)1u << bit));
    PORT.PDSC10 = ~(tmp & ~((uint32)1u << bit));
    PORT.PDSC10 = (tmp & ~((uint32)1u << bit));

    /* Set PODC (open-drain) - protected write */
    tmp = PORTPODC10;
    PORTPPCMD10 = 0xA5u;
    PORTPODC10 = (tmp | ((uint32)1u << bit));
    PORTPODC10 = ~(tmp | ((uint32)1u << bit));
    PORTPODC10 = (tmp | ((uint32)1u << bit));

    /* PBDC=1: bidirectional control (critical for I2C!) */
    PORT.PBDC10 |= ((uint16)1u << bit);

    /* AF2: PFC=1, PFCE=0, PFCAE=0 */
    PORTPFC10   |= ((uint16)1u << bit);
    PORTPFCE10  &= ~((uint16)1u << bit);
    PORTPFCAE10 &= ~((uint16)1u << bit);

    /* Switch to alt function, output mode */
    PORTPMC10 |= ((uint16)1u << bit);
    PORTPM10  &= ~((uint16)1u << bit);
}

static void riic0_pins_init(void)
{
    riic0_pin_setup(BOARD_I2C_SCL_BIT);
    riic0_pin_setup(BOARD_I2C_SDA_BIT);
}

/* ---- RIIC0 peripheral init ---- */

void hal_riic_slave_init(uint8 slave_addr,
                         hal_riic_slave_write_cb on_write,
                         hal_riic_slave_read_cb  on_read)
{
    g_on_write = on_write;
    g_on_read  = on_read;

    /* Configure I2C pins */
    riic0_pins_init();

    /* Mask all RIIC0 interrupts, clear flags, set table reference */
    ICRIIC0TI  |= ICR_MK;
    ICRIIC0EE  |= ICR_MK;
    ICRIIC0RI  |= ICR_MK;
    ICRIIC0TEI |= ICR_MK;
    ICRIIC0TI  &= ~ICR_RF;
    ICRIIC0EE  &= ~ICR_RF;
    ICRIIC0RI  &= ~ICR_RF;
    ICRIIC0TEI &= ~ICR_RF;
    ICRIIC0TI  |= ICR_TB;
    ICRIIC0EE  |= ICR_TB;
    ICRIIC0RI  |= ICR_TB;
    ICRIIC0TEI |= ICR_TB;

    /* Three-step reset (from Smart Configurator pattern) */
    RIIC0.CR1.UINT32 &= 0x0Cu;         /* Disable, keep SDAO/SCLO */
    RIIC0.CR1.UINT32 |= 0x40u;         /* Set IICRST */
    RIIC0.CR1.UINT32 |= 0xC0u;         /* Set ICE + IICRST */

    /* Slave address 0 (7-bit format) */
    RIIC0.SAR0.UINT32 = (uint32)(slave_addr << 1);

    /* Enable slave address 0 + general call */
    RIIC0.SER.UINT32 = 0x09u;

    /* MR1: CKS=/8 (40 MHz / 8 = 5 MHz internal reference) */
    RIIC0.MR1.UINT32 = 0x30u;

    /* BRL for SCL synchronization */
    RIIC0.BRL.UINT32 = 0xF7u;          /* 0xE0 | 0x17 */

    /* MR3: single-stage noise filter */
    RIIC0.MR3.UINT32 = 0x00u;

    /* FER: SCL sync + noise filter + NACK suspension */
    RIIC0.FER.UINT32 = 0x70u;

    /* IER: TIE, TEIE, RIE, STIE, SPIE, NAKIE (critical for SR2 flags) */
    RIIC0.IER.UINT32 = 0xFCu;

    /* Cancel internal reset (keep ICE=1) */
    RIIC0.CR1.UINT32 &= 0x8Cu;

    /* Synchronization barrier */
    { volatile uint32 sync = RIIC0.MR1.UINT32; (void)sync; }
    __nop();

    /* Init state — address starts at 0x0000 (EEPROM power-on default) */
    g_slave_state = ST_IDLE;
    g_reg_addr    = 0x0000u;

    /* Clear flags and unmask RIIC0 interrupts */
    ICRIIC0TI  &= ~ICR_RF;
    ICRIIC0EE  &= ~ICR_RF;
    ICRIIC0RI  &= ~ICR_RF;
    ICRIIC0TEI &= ~ICR_RF;
    ICRIIC0TI  &= ~ICR_MK;
    ICRIIC0EE  &= ~ICR_MK;
    ICRIIC0RI  &= ~ICR_MK;
    ICRIIC0TEI &= ~ICR_MK;
}

/* ---- Interrupt handlers ---- */

/*
 * TI interrupt (ch76): Transmit Data Empty
 * Fired when master reads additional bytes after the first.
 * Auto-increments address with 16-bit wrap.
 */
#pragma interrupt hal_riic0_isr_ti(enable=false, channel=76, fpu=true, callt=false)
void hal_riic0_isr_ti(void)
{
    uint8 val = 0xFFu;

    if (g_on_read != (void *)0)
    {
        val = g_on_read(g_reg_addr);
    }

    RIIC0.DRT.UINT32 = (uint32)val;
    g_reg_addr++;               /* 16-bit wrap at 0xFFFF -> 0x0000 */
    g_slave_state = ST_SEND_DONE;
}

/*
 * EE interrupt (ch77): Error/Event (START, STOP, NACK, AL)
 *
 * Note: g_reg_addr is NOT reset on STOP — this preserves the
 * current address for EEPROM-style current-address reads.
 */
#pragma interrupt hal_riic0_isr_ee(enable=false, channel=77, fpu=true, callt=false)
void hal_riic0_isr_ee(void)
{
    uint32 sr2 = RIIC0.SR2.UINT32;

    if ((sr2 & SR2_START) != 0u)
    {
        RIIC0.SR2.UINT32 &= ~(uint32)SR2_START;
    }

    if ((sr2 & SR2_NACKF) != 0u)
    {
        (void)RIIC0.DRR.UINT32;        /* Dummy read */
        RIIC0.SR2.UINT32 &= ~(uint32)SR2_NACKF;
        g_slave_state = ST_IDLE;
    }

    if ((sr2 & SR2_STOP) != 0u)
    {
        RIIC0.SR2.UINT32 &= ~(uint32)SR2_STOP;
        /* Don't reset state if RDRF pending (STOP+RDRF race) */
        if ((sr2 & SR2_RDRF) == 0u)
        {
            g_slave_state = ST_IDLE;
        }
    }

    if ((sr2 & SR2_AL) != 0u)
    {
        RIIC0.SR2.UINT32 &= ~(uint32)SR2_AL;
        g_slave_state = ST_IDLE;
    }
}

/*
 * RI interrupt (ch78): Receive Data Full
 *
 * Handles:
 *   - Address match (AAS0): start new transaction
 *   - Addr high byte: store upper 8 bits of 16-bit address
 *   - Addr low byte:  store lower 8 bits, address now fully set
 *   - Data bytes:     write to register via callback, auto-increment
 */
#pragma interrupt hal_riic0_isr_ri(enable=false, channel=78, fpu=true, callt=false)
void hal_riic0_isr_ri(void)
{
    uint8  data;
    uint32 sr1 = RIIC0.SR1.UINT32;

    /* Address match (AAS0 flag) */
    if ((sr1 & 0x01u) != 0u)
    {
        RIIC0.SR1.UINT32 &= ~0x01u;    /* Clear AAS0 */
        data = (uint8)RIIC0.DRR.UINT32; /* Dummy read to release SCL */

        if ((RIIC0.CR2.UINT32 & 0x20u) != 0u)
        {
            /* Master READ: current-address read (use existing g_reg_addr) */
            uint8 val = 0xFFu;
            g_slave_state = ST_SENDING_DATA;
            if (g_on_read != (void *)0)
            {
                val = g_on_read(g_reg_addr);
            }

            RIIC0.DRT.UINT32 = (uint32)val;

            DBG_PUTS("\nRD ");
            DBG_HEX8((uint8)(g_reg_addr >> 8));
            DBG_HEX8((uint8)g_reg_addr);
            DBG_PUTS("=");
            DBG_HEX8(val);

            g_reg_addr++;       /* Auto-increment with wrap */
        }
        else
        {
            /* Master WRITE: expect 2-byte address next */
            g_slave_state = ST_ADDR_HI;
            DBG_PUTS("\nWR ");
        }
        return;
    }

    /* Data phase */
    data = (uint8)RIIC0.DRR.UINT32;

    if (g_slave_state == ST_ADDR_HI)
    {
        /* First byte after slave address = addr high byte */
        g_reg_addr = (uint16)((uint16)data << 8);
        g_slave_state = ST_ADDR_LO;
    }
    else if (g_slave_state == ST_ADDR_LO)
    {
        /* Second byte = addr low byte, address now complete */
        g_reg_addr |= (uint16)data;
        g_slave_state = ST_RECEIVING_DATA;

        DBG_HEX8((uint8)(g_reg_addr >> 8));
        DBG_HEX8((uint8)g_reg_addr);
    }
    else if (g_slave_state == ST_RECEIVING_DATA)
    {
        /* Subsequent bytes = register data */
        if (g_on_write != (void *)0)
        {
            g_on_write(g_reg_addr, data);
        }
        g_reg_addr++;               /* Auto-increment with wrap */

        DBG_PUTS("=");
        DBG_HEX8(data);

        /* Handle STOP+RDRF race */
        if ((RIIC0.SR2.UINT32 & SR2_STOP) != 0u)
        {
            RIIC0.SR2.UINT32 &= ~(uint32)SR2_STOP;
            g_slave_state = ST_IDLE;
        }
    }
    else if ((g_slave_state == ST_SENDING_DATA) ||
             (g_slave_state == ST_SEND_DONE))
    {
        (void)data;     /* Ignore data during TX phase */
    }
    else
    {
        /* No action required */
    }
}

/*
 * TEI interrupt (ch79): Transmit End
 */
#pragma interrupt hal_riic0_isr_tei(enable=false, channel=79, fpu=true, callt=false)
void hal_riic0_isr_tei(void)
{
    (void)RIIC0.DRR.UINT32;    /* Dummy read */
    g_slave_state = ST_SENDING_DATA;
}
