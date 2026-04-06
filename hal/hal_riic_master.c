/*
 * hal_riic_master.c - RIIC0 hardware I2C master driver (polling)
 *
 * Polling-based master mode for RIIC0.
 * Pin config identical to slave (P10_2 SDA, P10_3 SCL, AF2, open-drain, PBDC).
 * Proven working on 983HH with PCF8574A at ~94 kHz.
 *
 * RIIC register access: UINT32 with value in low byte.
 */

#include "hal_riic_master.h"
#include "hal_gpio.h"
#include "board.h"

#define TIMEOUT     0x80000u

/* SR2 bit definitions */
#define SR2_TDRE    0x80u
#define SR2_TEND    0x40u
#define SR2_RDRF    0x20u
#define SR2_NACKF   0x10u
#define SR2_STOP    0x08u
#define SR2_START   0x04u

/* CR2 bit definitions */
#define CR2_BBSY    0x80u
#define CR2_SP      0x08u
#define CR2_RS      0x04u
#define CR2_ST      0x02u

/* ---- Pin configuration (shared with slave) ---- */

static void riic0_pin_setup(uint8 bit)
{
    uint32 tmp;

    PORTPIBC10  &= ~(uint16)(1u << bit);
    PORT.PBDC10 &= ~(uint16)(1u << bit);
    PORTPM10    |= (uint16)(1u << bit);
    PORTPMC10   &= ~(uint16)(1u << bit);
    PORTPIPC10  &= ~(uint16)(1u << bit);

    /* Clear PDSC - protected write */
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

    /* PBDC=1 (critical for I2C bidirectional) */
    PORT.PBDC10 |= (uint16)(1u << bit);

    /* AF2: PFC=1, PFCE=0, PFCAE=0 */
    PORTPFC10   |= (uint16)(1u << bit);
    PORTPFCE10  &= ~(uint16)(1u << bit);
    PORTPFCAE10 &= ~(uint16)(1u << bit);

    PORTPMC10 |= (uint16)(1u << bit);
    PORTPM10  &= ~(uint16)(1u << bit);
}

/* ---- Polling helpers ---- */

static uint8 wait_tdre(void)
{
    volatile uint32 t = TIMEOUT;
    while (!(RIIC0.SR2.UINT32 & SR2_TDRE) && --t)
        ;
    return (t > 0u) ? 1u : 0u;
}

static uint8 wait_tend(void)
{
    volatile uint32 t = TIMEOUT;
    while (!(RIIC0.SR2.UINT32 & SR2_TEND) && --t)
        ;
    return (t > 0u) ? 1u : 0u;
}

static uint8 wait_rdrf(void)
{
    volatile uint32 t = TIMEOUT;
    while (!(RIIC0.SR2.UINT32 & SR2_RDRF) && --t)
        ;
    return (t > 0u) ? 1u : 0u;
}

static uint8 wait_stop(void)
{
    volatile uint32 t = TIMEOUT;
    while (!(RIIC0.SR2.UINT32 & SR2_STOP) && --t)
        ;
    return (t > 0u) ? 1u : 0u;
}

static void issue_stop(void)
{
    RIIC0.SR2.UINT32 &= ~(uint32)SR2_STOP;
    RIIC0.CR2.UINT32 = CR2_SP;
    wait_stop();
    RIIC0.SR2.UINT32 &= ~(uint32)(SR2_NACKF | SR2_STOP);
}

/* ---- Public API ---- */

void hal_riic_master_init(void)
{
    riic0_pin_setup(BOARD_I2C_SCL_BIT);
    riic0_pin_setup(BOARD_I2C_SDA_BIT);

    /* Three-step reset */
    RIIC0.CR1.UINT32 &= 0x0Cu;         /* Disable */
    RIIC0.CR1.UINT32 |= 0x40u;         /* IICRST */
    RIIC0.CR1.UINT32 |= 0xC0u;         /* ICE + IICRST */

    /* MR1: CKS=/8 (40 MHz / 8 = 5 MHz) */
    RIIC0.MR1.UINT32 = 0x30u;

    /* BRL/BRH for ~94 kHz at 5 MHz reference */
    RIIC0.BRL.UINT32 = 0xF7u;          /* 0xE0 | 0x17 (low=23) */
    RIIC0.BRH.UINT32 = 0xF4u;          /* 0xE0 | 0x14 (high=20) */

    /* MR3: single-stage noise filter */
    RIIC0.MR3.UINT32 = 0x00u;

    /* FER: SCLE + NFE + NACKE + MALE */
    RIIC0.FER.UINT32 = 0x72u;

    /* No slave addresses in master mode */
    RIIC0.SER.UINT32 = 0x00u;

    /* IER: enable status flags (TDRE, TEND, etc. need IER bits set) */
    RIIC0.IER.UINT32 = 0xFCu;

    /* Cancel internal reset */
    RIIC0.CR1.UINT32 &= 0x8Cu;

    /* Synchronization barrier */
    { volatile uint32 sync = RIIC0.MR1.UINT32; (void)sync; }
    __nop();

    /* Allow bus-free detection */
    { volatile uint32 d = 1000u; while (d--) ; }
}

uint8 hal_riic_master_write(uint8 addr_7bit, const uint8 *data, uint8 len)
{
    uint8 i;

    /* Check bus not busy */
    if (RIIC0.CR2.UINT32 & CR2_BBSY)
        return 0u;

    /* Issue START */
    RIIC0.CR2.UINT32 = CR2_ST;

    if (!wait_tdre())
        return 0u;

    /* Send slave address + W */
    RIIC0.DRT.UINT32 = (uint32)(addr_7bit << 1);

    if (!wait_tend())
        { issue_stop(); return 0u; }
    if (RIIC0.SR2.UINT32 & SR2_NACKF)
        { issue_stop(); return 0u; }

    /* Send data bytes */
    for (i = 0u; i < len; i++)
    {
        RIIC0.DRT.UINT32 = (uint32)data[i];

        if (!wait_tend())
            { issue_stop(); return 0u; }
        if (RIIC0.SR2.UINT32 & SR2_NACKF)
            { issue_stop(); return 0u; }
    }

    /* Issue STOP */
    issue_stop();
    return 1u;
}

uint8 hal_riic_master_read(uint8 addr_7bit, uint8 *data, uint8 len)
{
    uint8 i;

    if (RIIC0.CR2.UINT32 & CR2_BBSY)
        return 0u;

    /* Issue START */
    RIIC0.CR2.UINT32 = CR2_ST;

    if (!wait_tdre())
        return 0u;

    /* Send slave address + R */
    RIIC0.DRT.UINT32 = (uint32)((addr_7bit << 1) | 1u);

    if (!wait_rdrf())
        { issue_stop(); return 0u; }
    if (RIIC0.SR2.UINT32 & SR2_NACKF)
        { issue_stop(); return 0u; }

    /* Read data bytes */
    for (i = 0u; i < len; i++)
    {
        if (i == len - 1u)
        {
            /* Last byte: set NACK + STOP before reading */
            RIIC0.MR3.UINT32 |= 0x10u;     /* ACKBT = NACK */
        }

        if (i > 0u)
        {
            if (!wait_rdrf())
                { issue_stop(); return 0u; }
        }

        data[i] = (uint8)RIIC0.DRR.UINT32;
    }

    issue_stop();
    RIIC0.MR3.UINT32 &= ~0x10u;    /* Restore ACK */
    return 1u;
}
