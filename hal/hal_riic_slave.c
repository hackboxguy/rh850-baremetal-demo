/*
 * hal_riic_slave.c - RIIC0 hardware I2C slave driver (interrupt-driven)
 *
 * Interrupt-driven slave with register-based protocol.
 * Master sends: [slave_addr+W] [reg_addr] [data...]
 * Master reads: [slave_addr+W] [reg_addr] [slave_addr+R] [data...]
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
#define ICR_MK      (1u << 7)       /* Interrupt mask */
#define ICR_TB      (1u << 6)       /* Table reference */
#define ICR_RF      (1u << 12)      /* Request flag */

/* RIIC SR2 bit definitions */
#define SR2_TDRE    0x80u
#define SR2_TEND    0x40u
#define SR2_RDRF    0x20u
#define SR2_NACKF   0x10u
#define SR2_STOP    0x08u
#define SR2_START   0x04u
#define SR2_AL      0x02u

/* Slave state machine */
#define ST_IDLE             0u
#define ST_ADDR_RECEIVED    1u
#define ST_RECEIVING_DATA   2u
#define ST_SENDING_DATA     3u
#define ST_SEND_DONE        4u

static volatile uint8 g_slave_state;
static volatile uint8 g_reg_addr;
static volatile uint8 g_reg_addr_set;

static hal_riic_slave_write_cb g_on_write;
static hal_riic_slave_read_cb  g_on_read;

/* ---- Pin configuration ---- */

static void riic0_pin_setup(uint8 bit)
{
    uint32 tmp;

    /* Clear input buffer and bidirectional first */
    PORTPIBC10  &= ~(uint16)(1u << bit);
    PORT.PBDC10 &= ~(uint16)(1u << bit);
    PORTPM10    |= (uint16)(1u << bit);       /* Input initially */
    PORTPMC10   &= ~(uint16)(1u << bit);      /* GPIO initially */
    PORTPIPC10  &= ~(uint16)(1u << bit);      /* No PIPC */

    /* Clear PDSC (drive strength) - protected write */
    tmp = PORT.PDSC10;
    PORTPPCMD10 = 0xA5u;
    PORT.PDSC10 = (tmp & ~(uint32)(1u << bit));
    PORT.PDSC10 = ~(tmp & ~(uint32)(1u << bit));
    PORT.PDSC10 = (tmp & ~(uint32)(1u << bit));

    /* Set PODC (open-drain) - protected write */
    tmp = PORTPODC10;
    PORTPPCMD10 = 0xA5u;
    PORTPODC10 = (tmp | (uint32)(1u << bit));
    PORTPODC10 = ~(tmp | (uint32)(1u << bit));
    PORTPODC10 = (tmp | (uint32)(1u << bit));

    /* PBDC=1: bidirectional control (critical for I2C!) */
    PORT.PBDC10 |= (uint16)(1u << bit);

    /* AF2: PFC=1, PFCE=0, PFCAE=0 */
    PORTPFC10   |= (uint16)(1u << bit);
    PORTPFCE10  &= ~(uint16)(1u << bit);
    PORTPFCAE10 &= ~(uint16)(1u << bit);

    /* Switch to alt function, output mode */
    PORTPMC10 |= (uint16)(1u << bit);
    PORTPM10  &= ~(uint16)(1u << bit);
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

    /* Init state */
    g_slave_state  = ST_IDLE;
    g_reg_addr     = 0u;
    g_reg_addr_set = 0u;

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
 */
#pragma interrupt hal_riic0_isr_ti(enable=false, channel=76, fpu=true, callt=false)
void hal_riic0_isr_ti(void)
{
    uint8 val = 0xFFu;

    if (g_on_read && g_reg_addr < 0xFFu)
        val = g_on_read(g_reg_addr);

    RIIC0.DRT.UINT32 = (uint32)val;
    g_reg_addr++;
    g_slave_state = ST_SEND_DONE;
}

/*
 * EE interrupt (ch77): Error/Event (START, STOP, NACK, AL)
 */
#pragma interrupt hal_riic0_isr_ee(enable=false, channel=77, fpu=true, callt=false)
void hal_riic0_isr_ee(void)
{
    uint32 sr2 = RIIC0.SR2.UINT32;

    if (sr2 & SR2_START)
    {
        RIIC0.SR2.UINT32 &= ~(uint32)SR2_START;
    }

    if (sr2 & SR2_NACKF)
    {
        (void)RIIC0.DRR.UINT32;        /* Dummy read */
        RIIC0.SR2.UINT32 &= ~(uint32)SR2_NACKF;
        g_slave_state  = ST_IDLE;
        g_reg_addr_set = 0u;
    }

    if (sr2 & SR2_STOP)
    {
        RIIC0.SR2.UINT32 &= ~(uint32)SR2_STOP;
        /* Don't reset state if RDRF pending (STOP+RDRF race) */
        if (!(sr2 & SR2_RDRF))
        {
            g_slave_state  = ST_IDLE;
            g_reg_addr_set = 0u;
        }
    }

    if (sr2 & SR2_AL)
    {
        RIIC0.SR2.UINT32 &= ~(uint32)SR2_AL;
        g_slave_state = ST_IDLE;
    }
}

/*
 * RI interrupt (ch78): Receive Data Full
 * Handles address match, register address byte, and data bytes.
 */
#pragma interrupt hal_riic0_isr_ri(enable=false, channel=78, fpu=true, callt=false)
void hal_riic0_isr_ri(void)
{
    uint8  data;
    uint32 sr1 = RIIC0.SR1.UINT32;

    /* Address match (AAS0 flag) */
    if (sr1 & 0x01u)
    {
        RIIC0.SR1.UINT32 &= ~0x01u;    /* Clear AAS0 */
        data = (uint8)RIIC0.DRR.UINT32; /* Dummy read to release SCL */

        if (RIIC0.CR2.UINT32 & 0x20u)
        {
            /* Master READ: send first byte */
            uint8 val = 0xFFu;
            g_slave_state = ST_SENDING_DATA;
            if (g_on_read && g_reg_addr < 0xFFu)
                val = g_on_read(g_reg_addr);

            RIIC0.DRT.UINT32 = (uint32)val;

            DBG_PUTS("\nI2C RD r=0x");
            DBG_HEX8(g_reg_addr);
            DBG_PUTS(" v=0x");
            DBG_HEX8(val);

            g_reg_addr++;
        }
        else
        {
            /* Master WRITE: wait for register address */
            g_slave_state  = ST_ADDR_RECEIVED;
            g_reg_addr_set = 0u;
            DBG_PUTS("\nI2C WR");
        }
        return;
    }

    /* Data phase */
    data = (uint8)RIIC0.DRR.UINT32;

    if (g_slave_state == ST_ADDR_RECEIVED && !g_reg_addr_set)
    {
        /* First data byte = register address */
        g_reg_addr     = data;
        g_reg_addr_set = 1u;
        g_slave_state  = ST_RECEIVING_DATA;

        DBG_PUTS(" r=0x");
        DBG_HEX8(data);
    }
    else if (g_slave_state == ST_RECEIVING_DATA)
    {
        /* Subsequent data bytes = register values */
        if (g_on_write)
            g_on_write(g_reg_addr, data);
        g_reg_addr++;

        DBG_PUTS(" v=0x");
        DBG_HEX8(data);

        /* Handle STOP+RDRF race */
        if (RIIC0.SR2.UINT32 & SR2_STOP)
        {
            RIIC0.SR2.UINT32 &= ~(uint32)SR2_STOP;
            g_slave_state  = ST_IDLE;
            g_reg_addr_set = 0u;
        }
    }
    else if (g_slave_state == ST_SENDING_DATA ||
             g_slave_state == ST_SEND_DONE)
    {
        (void)data;     /* Ignore data during TX phase */
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
