/*
 * hal_riic1_master.c - RIIC1 hardware I2C master driver (polling)
 *
 * Second I2C bus: P8_0 (SDA1), P8_1 (SCL1), AF2.
 * Register base: 0xFFCA0080 (identical layout to RIIC0).
 * 100 kHz standard mode with CKS=/8 (5 MHz ref from 40 MHz PCLK).
 *
 * Pin setup follows same pattern as RIIC0: open-drain + PBDC.
 * Port 8 has no PDSC/PIPC registers (skip those steps).
 */

#include "hal_riic1_master.h"
#include "hal_gpio.h"

#define SDA1_BIT    0u      /* P8_0 */
#define SCL1_BIT    1u      /* P8_1 */

#define TIMEOUT     0x80000u

/* SR2 bit definitions */
#define SR2_TDRE    0x80u
#define SR2_TEND    0x40u
#define SR2_RDRF    0x20u
#define SR2_NACKF   0x10u
#define SR2_STOP    0x08u

/* CR2 bit definitions */
#define CR2_BBSY    0x80u
#define CR2_SP      0x08u
#define CR2_ST      0x02u

/* ---- Pin configuration ---- */

static void riic1_pin_setup(uint8 bit)
{
    uint32 tmp;

    /* Clear input buffer and bidirectional first */
    PORTPIBC8   &= ~((uint16)1u << bit);
    PORTPBDC8   &= ~((uint16)1u << bit);
    PORTPM8     |= ((uint16)1u << bit);        /* Input initially */
    PORTPMC8    &= ~((uint16)1u << bit);        /* GPIO initially */
    /* No PIPC8 or PDSC8 on port 8 */

    /* Set PODC (open-drain) - protected write */
    tmp = PORTPODC8;
    PORTPPCMD8 = 0xA5u;
    PORTPODC8 = (tmp | ((uint32)1u << bit));
    PORTPODC8 = ~(tmp | ((uint32)1u << bit));
    PORTPODC8 = (tmp | ((uint32)1u << bit));

    /* PBDC=1 (bidirectional control — critical for I2C) */
    PORTPBDC8 |= ((uint16)1u << bit);

    /* AF2: PFC=1, PFCE=0, PFCAE=0 */
    PORTPFC8    |= ((uint16)1u << bit);
    PORTPFCE8   &= ~((uint16)1u << bit);
    PORTPFCAE8  &= ~((uint16)1u << bit);

    /* Switch to alt function, output mode */
    PORTPMC8 |= ((uint16)1u << bit);
    PORTPM8  &= ~((uint16)1u << bit);
}

/* ---- Polling helpers ---- */

static uint8 wait_tdre(void)
{
    volatile uint32 t = TIMEOUT;
    while (t != 0u)
    {
        if ((RIIC1.SR2.UINT32 & SR2_TDRE) != 0u)
        {
            break;
        }
        t--;
    }
    return (t != 0u) ? 1u : 0u;
}

static uint8 wait_tend(void)
{
    volatile uint32 t = TIMEOUT;
    while (t != 0u)
    {
        if ((RIIC1.SR2.UINT32 & SR2_TEND) != 0u)
        {
            break;
        }
        t--;
    }
    return (t != 0u) ? 1u : 0u;
}

static uint8 wait_rdrf(void)
{
    volatile uint32 t = TIMEOUT;
    while (t != 0u)
    {
        if ((RIIC1.SR2.UINT32 & SR2_RDRF) != 0u)
        {
            break;
        }
        t--;
    }
    return (t != 0u) ? 1u : 0u;
}

static uint8 wait_stop(void)
{
    volatile uint32 t = TIMEOUT;
    while (t != 0u)
    {
        if ((RIIC1.SR2.UINT32 & SR2_STOP) != 0u)
        {
            break;
        }
        t--;
    }
    return (t != 0u) ? 1u : 0u;
}

static void issue_stop(void)
{
    RIIC1.SR2.UINT32 &= ~(uint32)SR2_STOP;
    RIIC1.CR2.UINT32 = CR2_SP;
    (void)wait_stop();
    RIIC1.SR2.UINT32 &= ~((uint32)SR2_NACKF | (uint32)SR2_STOP);
}

/* ---- Public API ---- */

void hal_riic1_master_init(void)
{
    riic1_pin_setup(SCL1_BIT);
    riic1_pin_setup(SDA1_BIT);

    /* Three-step reset */
    RIIC1.CR1.UINT32 &= 0x0Cu;
    RIIC1.CR1.UINT32 |= 0x40u;
    RIIC1.CR1.UINT32 |= 0xC0u;

    /* MR1: CKS=/8 (40 MHz / 8 = 5 MHz) */
    RIIC1.MR1.UINT32 = 0x30u;

    /* BRL/BRH for ~100 kHz at 5 MHz reference */
    RIIC1.BRL.UINT32 = 0xF7u;      /* 0xE0 | 0x17 (low=23) */
    RIIC1.BRH.UINT32 = 0xF4u;      /* 0xE0 | 0x14 (high=20) */

    /* MR3: single-stage noise filter */
    RIIC1.MR3.UINT32 = 0x00u;

    /* FER: SCLE + NFE + NACKE + MALE */
    RIIC1.FER.UINT32 = 0x72u;

    /* No slave addresses in master mode */
    RIIC1.SER.UINT32 = 0x00u;

    /* IER: enable status flags (needed even for polling) */
    RIIC1.IER.UINT32 = 0xFCu;

    /* Cancel internal reset */
    RIIC1.CR1.UINT32 &= 0x8Cu;

    /* Synchronization barrier */
    { volatile uint32 sync = RIIC1.MR1.UINT32; (void)sync; }
    __nop();

    /* Allow bus-free detection */
    { volatile uint32 d = 1000u; while (d-- != 0u) { ; } }
}

uint8 hal_riic1_master_probe(uint8 addr_7bit)
{
    uint8 ack = 0u;

    if ((RIIC1.CR2.UINT32 & CR2_BBSY) != 0u)
    {
        return 0u;
    }

    /* Issue START */
    RIIC1.CR2.UINT32 = CR2_ST;

    if (wait_tdre() == 0u)
    {
        return 0u;
    }

    /* Send address + W */
    RIIC1.DRT.UINT32 = (uint32)addr_7bit << 1;

    if (wait_tend() == 0u)
    {
        issue_stop();
        return 0u;
    }

    /* Check ACK */
    if ((RIIC1.SR2.UINT32 & SR2_NACKF) == 0u)
    {
        ack = 1u;
    }

    /* Issue STOP */
    issue_stop();
    return ack;
}

uint8 hal_riic1_master_write(uint8 addr_7bit, const uint8 *data, uint8 len)
{
    uint8 i;

    if ((RIIC1.CR2.UINT32 & CR2_BBSY) != 0u)
    {
        return 0u;
    }

    RIIC1.CR2.UINT32 = CR2_ST;

    if (wait_tdre() == 0u)
    {
        return 0u;
    }

    RIIC1.DRT.UINT32 = (uint32)addr_7bit << 1;

    if (wait_tend() == 0u)
    {
        issue_stop();
        return 0u;
    }
    if ((RIIC1.SR2.UINT32 & SR2_NACKF) != 0u)
    {
        issue_stop();
        return 0u;
    }

    for (i = 0u; i < len; i++)
    {
        RIIC1.DRT.UINT32 = (uint32)data[i];

        if (wait_tend() == 0u)
        {
            issue_stop();
            return 0u;
        }
        if ((RIIC1.SR2.UINT32 & SR2_NACKF) != 0u)
        {
            issue_stop();
            return 0u;
        }
    }

    issue_stop();
    return 1u;
}

uint8 hal_riic1_master_read(uint8 addr_7bit, uint8 *data, uint8 len)
{
    uint8 i;

    if ((RIIC1.CR2.UINT32 & CR2_BBSY) != 0u)
    {
        return 0u;
    }

    RIIC1.CR2.UINT32 = CR2_ST;

    if (wait_tdre() == 0u)
    {
        return 0u;
    }

    RIIC1.DRT.UINT32 = ((uint32)addr_7bit << 1) | 1u;

    if (wait_rdrf() == 0u)
    {
        issue_stop();
        return 0u;
    }
    if ((RIIC1.SR2.UINT32 & SR2_NACKF) != 0u)
    {
        issue_stop();
        return 0u;
    }

    for (i = 0u; i < len; i++)
    {
        if (i == (len - 1u))
        {
            RIIC1.MR3.UINT32 |= 0x10u;     /* ACKBT = NACK */
        }

        if (i > 0u)
        {
            if (wait_rdrf() == 0u)
            {
                issue_stop();
                return 0u;
            }
        }

        data[i] = (uint8)RIIC1.DRR.UINT32;
    }

    issue_stop();
    RIIC1.MR3.UINT32 &= ~0x10u;
    return 1u;
}
