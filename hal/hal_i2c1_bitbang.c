/*
 * hal_i2c1_bitbang.c - Bit-banged I2C master on bus 1
 *
 * P8_0 (SDA1), P8_1 (SCL1).
 * Open-drain style: HIGH=input (pull-up), LOW=drive output.
 * Same approach as the BIOS software I2C on this bus.
 *
 * Includes bus recovery (9-clock SCL) and probe function for scanning.
 */

#include "hal_i2c1_bitbang.h"
#include "hal_gpio.h"

#define SDA1_BIT    0u      /* P8_0 */
#define SCL1_BIT    1u      /* P8_1 */

/* Half-bit delay for ~100 kHz at 80 MHz CPU */
#define I2C1_DELAY_COUNT  200u

/* ---- Low-level pin control ---- */

static void i2c1_delay(void)
{
    volatile uint32 d = I2C1_DELAY_COUNT;
    while (d-- != 0u)
    {
        ;
    }
}

static void sda1_high(void)
{
    PORTPMSR8 = PSR_SET(SDA1_BIT);     /* PM=1 (input, pull-up pulls high) */
}

static void sda1_low(void)
{
    PORTPSR8  = PSR_CLR(SDA1_BIT);     /* P=0 */
    PORTPMSR8 = PSR_CLR(SDA1_BIT);     /* PM=0 (output, drives low) */
}

static void scl1_high(void)
{
    PORTPMSR8 = PSR_SET(SCL1_BIT);
}

static void scl1_low(void)
{
    PORTPSR8  = PSR_CLR(SCL1_BIT);
    PORTPMSR8 = PSR_CLR(SCL1_BIT);
}

static uint8 sda1_read(void)
{
    return ((PORTPPR8 & ((uint16)1u << SDA1_BIT)) != 0u) ? 1u : 0u;
}

/* ---- I2C protocol primitives ---- */

static void i2c1_start(void)
{
    sda1_high();
    scl1_high();
    i2c1_delay();
    sda1_low();         /* SDA falls while SCL high = START */
    i2c1_delay();
    scl1_low();
}

static void i2c1_stop(void)
{
    sda1_low();
    scl1_high();
    i2c1_delay();
    sda1_high();        /* SDA rises while SCL high = STOP */
    i2c1_delay();
}

static uint8 i2c1_write_byte(uint8 data)
{
    uint8 i;
    uint8 tx_data = data;
    uint8 ack;

    for (i = 0u; i < 8u; i++)
    {
        if ((tx_data & 0x80u) != 0u)
        {
            sda1_high();
        }
        else
        {
            sda1_low();
        }

        i2c1_delay();
        scl1_high();
        i2c1_delay();
        scl1_low();

        tx_data <<= 1;
    }

    /* ACK clock */
    sda1_high();
    i2c1_delay();
    scl1_high();
    i2c1_delay();
    ack = (sda1_read() == 0u) ? 1u : 0u;
    scl1_low();

    return ack;
}

static uint8 i2c1_read_byte(uint8 ack)
{
    uint8 i;
    uint8 data = 0u;

    sda1_high();

    for (i = 0u; i < 8u; i++)
    {
        data <<= 1;
        i2c1_delay();
        scl1_high();
        i2c1_delay();
        if (sda1_read() != 0u)
        {
            data |= 0x01u;
        }
        scl1_low();
    }

    /* Send ACK/NACK */
    if (ack != 0u)
    {
        sda1_low();
    }
    else
    {
        sda1_high();
    }
    i2c1_delay();
    scl1_high();
    i2c1_delay();
    scl1_low();
    sda1_high();

    return data;
}

/* ---- Public API ---- */

void hal_i2c1_bitbang_init(void)
{
    uint8 i;

    /* Set P8_0 and P8_1 as GPIO */
    PORTPMCSR8 = PSR_CLR(SDA1_BIT);
    PORTPMCSR8 = PSR_CLR(SCL1_BIT);

    /* Enable input buffer for pin readback */
    PORTPIBC8 |= (uint16)(((uint16)1u << SDA1_BIT) | ((uint16)1u << SCL1_BIT));

    /* Release both lines */
    sda1_high();
    scl1_high();
    i2c1_delay();

    /* Bus recovery: clock SCL up to 9 times to free stuck slave */
    for (i = 0u; i < 9u; i++)
    {
        if (sda1_read() != 0u)
        {
            break;
        }
        scl1_high();
        i2c1_delay();
        scl1_low();
        i2c1_delay();
    }

    /* STOP to reset bus */
    sda1_low();
    i2c1_delay();
    scl1_high();
    i2c1_delay();
    sda1_high();
    i2c1_delay();
}

uint8 hal_i2c1_bitbang_probe(uint8 addr_7bit)
{
    uint8 ack;

    i2c1_start();
    ack = i2c1_write_byte((uint8)((uint32)addr_7bit << 1));
    i2c1_stop();

    return ack;
}

uint8 hal_i2c1_bitbang_write(uint8 addr_7bit, const uint8 *data, uint8 len)
{
    uint8 i;

    i2c1_start();

    if (i2c1_write_byte((uint8)((uint32)addr_7bit << 1)) == 0u)
    {
        i2c1_stop();
        return 0u;
    }

    for (i = 0u; i < len; i++)
    {
        if (i2c1_write_byte(data[i]) == 0u)
        {
            i2c1_stop();
            return 0u;
        }
    }

    i2c1_stop();
    return 1u;
}

uint8 hal_i2c1_bitbang_read(uint8 addr_7bit, uint8 *data, uint8 len)
{
    uint8 i;

    i2c1_start();

    if (i2c1_write_byte((uint8)(((uint32)addr_7bit << 1) | 1u)) == 0u)
    {
        i2c1_stop();
        return 0u;
    }

    for (i = 0u; i < len; i++)
    {
        data[i] = i2c1_read_byte((i < (len - 1u)) ? 1u : 0u);
    }

    i2c1_stop();
    return 1u;
}

/*
 * Combined write-then-read with repeated-start.
 * Sequence on the wire (no STOP between write and read phases):
 *   START + [addr+W] + [wr_data...] + RESTART + [addr+R] + [rd_data...] + STOP
 *
 * For bit-banged I2C, RESTART is just a START condition issued without
 * a preceding STOP — i.e., release SDA high, release SCL high, then drive
 * SDA low while SCL is still high.
 */
uint8 hal_i2c1_bitbang_write_read(uint8 addr_7bit,
                                  const uint8 *wr_data, uint8 wr_len,
                                  uint8 *rd_data, uint8 rd_len)
{
    uint8 i;

    /* --- WRITE phase --- */
    i2c1_start();

    if (i2c1_write_byte((uint8)((uint32)addr_7bit << 1)) == 0u)
    {
        i2c1_stop();
        return 0u;
    }

    for (i = 0u; i < wr_len; i++)
    {
        if (i2c1_write_byte(wr_data[i]) == 0u)
        {
            i2c1_stop();
            return 0u;
        }
    }

    /* --- REPEATED START (no STOP) ---
     * Release SDA high, release SCL high, then drive SDA low while
     * SCL is still high. This generates a START condition that the
     * slave recognizes as a continuation of the same transaction. */
    sda1_high();
    i2c1_delay();
    scl1_high();
    i2c1_delay();
    sda1_low();          /* RESTART: SDA falls while SCL high */
    i2c1_delay();
    scl1_low();

    /* --- READ phase --- */
    if (i2c1_write_byte((uint8)(((uint32)addr_7bit << 1) | 1u)) == 0u)
    {
        i2c1_stop();
        return 0u;
    }

    for (i = 0u; i < rd_len; i++)
    {
        rd_data[i] = i2c1_read_byte((i < (rd_len - 1u)) ? 1u : 0u);
    }

    i2c1_stop();
    return 1u;
}
