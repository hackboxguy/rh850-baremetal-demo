/*
 * hal_i2c_bitbang.c - Bit-banged I2C master driver
 *
 * Open-drain style using GPIO:
 *   HIGH = set pin as input (external pull-up pulls line high)
 *   LOW  = drive pin low, set as output
 *
 * ~100 kHz I2C bus speed (adjust I2C_DELAY_COUNT for your clock).
 * Proven working on 983HH board with PCF8574A.
 */

#include "hal_i2c_bitbang.h"
#include "hal_gpio.h"
#include "board.h"

#define SDA_BIT     BOARD_I2C_SDA_BIT
#define SCL_BIT     BOARD_I2C_SCL_BIT
#define I2C_MASK    ((uint16)(((uint16)1u << SDA_BIT) | ((uint16)1u << SCL_BIT)))

/* Half-bit delay count (~100 kHz at default 8 MHz clock) */
#define I2C_DELAY_COUNT  50u

/* ---- Low-level pin control ---- */

static void i2c_delay(void)
{
    volatile uint32 d = I2C_DELAY_COUNT;
    while (d-- != 0u)
    {
        ;
    }
}

static void sda_high(void)
{
    /* Release SDA: set as input, pull-up pulls high */
    PORTPMSR10 = PSR_SET(SDA_BIT);
}

static void sda_low(void)
{
    /* Drive SDA low: set port=0, then switch to output */
    PORTPSR10  = PSR_CLR(SDA_BIT);
    PORTPMSR10 = PSR_CLR(SDA_BIT);
}

static void scl_high(void)
{
    PORTPMSR10 = PSR_SET(SCL_BIT);
}

static void scl_low(void)
{
    PORTPSR10  = PSR_CLR(SCL_BIT);
    PORTPMSR10 = PSR_CLR(SCL_BIT);
}

static uint8 sda_read(void)
{
    return (PORTPPR10 & (uint16)(1u << SDA_BIT)) ? 1u : 0u;
}

static void i2c_gpio_mode_init(void)
{
    /* Force the pins back to plain GPIO before bit-banging.
     * A short power cycle or a previous RIIC app can leave AF2 selected,
     * and PM toggling alone does not take ownership back from the peripheral. */
    PORTPMC10   &= (uint16)~I2C_MASK;
    PORTPFC10   &= (uint16)~I2C_MASK;
    PORTPFCE10  &= (uint16)~I2C_MASK;
    PORTPFCAE10 &= (uint16)~I2C_MASK;
    PORTPIPC10  &= (uint16)~I2C_MASK;
    PORT.PBDC10 &= (uint16)~I2C_MASK;

    /* Start released/input until the bit-bang helpers drive a low level. */
    PORTPM10 |= I2C_MASK;
}

/* ---- I2C protocol primitives ---- */

static void i2c_start(void)
{
    sda_high();
    scl_high();
    i2c_delay();
    sda_low();      /* SDA falls while SCL high = START */
    i2c_delay();
    scl_low();
}

static void i2c_stop(void)
{
    sda_low();
    scl_high();
    i2c_delay();
    sda_high();     /* SDA rises while SCL high = STOP */
    i2c_delay();
}

/* Send 8 bits MSB-first, return 1 if ACK received */
static uint8 i2c_write_byte(uint8 data)
{
    uint8 i;
    uint8 ack;
    uint8 tx_data = data;

    for (i = 0u; i < 8u; i++)
    {
        if ((tx_data & 0x80u) != 0u)
        {
            sda_high();
        }
        else
        {
            sda_low();
        }

        i2c_delay();
        scl_high();
        i2c_delay();
        scl_low();

        tx_data <<= 1;
    }

    /* ACK clock: release SDA, clock SCL, read SDA */
    sda_high();
    i2c_delay();
    scl_high();
    i2c_delay();
    ack = (sda_read() == 0u) ? 1u : 0u;
    scl_low();

    return ack;
}

/* Read 8 bits MSB-first, send ACK (ack=1) or NACK (ack=0) */
static uint8 i2c_read_byte(uint8 ack)
{
    uint8 i;
    uint8 data = 0u;

    sda_high();     /* Release SDA for slave to drive */

    for (i = 0u; i < 8u; i++)
    {
        data <<= 1;
        i2c_delay();
        scl_high();
        i2c_delay();
        if (sda_read() != 0u)
        {
            data |= 0x01u;
        }
        scl_low();
    }

    /* Send ACK/NACK */
    if (ack != 0u)
    {
        sda_low();
    }
    else
    {
        sda_high();
    }
    i2c_delay();
    scl_high();
    i2c_delay();
    scl_low();
    sda_high();

    return data;
}

/* ---- Public API ---- */

void hal_i2c_bitbang_init(void)
{
    uint8 i;

    /* Force GPIO ownership before touching the lines. */
    i2c_gpio_mode_init();

    /* Enable input buffer for pin readback */
    PORTPIBC10 |= I2C_MASK;

    /* Release both lines (high via external pull-ups) */
    sda_high();
    scl_high();
    i2c_delay();

    /*
     * Bus recovery: if a slave is stuck mid-byte (holding SDA low),
     * clock SCL up to 9 times to let it finish. Each clock shifts
     * out one bit; after 8 data bits + 1 ACK bit, the slave releases
     * SDA. This handles power-cycle glitches where the MCU reset
     * but the slave (e.g. PCF8574A) kept its state.
     */
    for (i = 0u; i < 9u; i++)
    {
        if (sda_read() != 0u)
        {
            break;      /* SDA is high — bus is free */
        }
        scl_high();
        i2c_delay();
        scl_low();
        i2c_delay();
    }

    /* Generate STOP to reset bus state */
    sda_low();
    i2c_delay();
    scl_high();
    i2c_delay();
    sda_high();
    i2c_delay();
}

uint8 hal_i2c_bitbang_write(uint8 addr_7bit, const uint8 *data, uint8 len)
{
    uint8 i;
    uint8 ack;

    i2c_start();

    /* Send address + W */
    ack = i2c_write_byte((uint8)((uint32)addr_7bit << 1));
    if (!ack)
    {
        i2c_stop();
        return 0u;
    }

    /* Send data bytes */
    for (i = 0u; i < len; i++)
    {
        ack = i2c_write_byte(data[i]);
        if (!ack)
        {
            i2c_stop();
            return 0u;
        }
    }

    i2c_stop();
    return 1u;
}

uint8 hal_i2c_bitbang_read(uint8 addr_7bit, uint8 *data, uint8 len)
{
    uint8 i;

    i2c_start();

    /* Send address + R */
    if (!i2c_write_byte((uint8)(((uint32)addr_7bit << 1) | 1u)))
    {
        i2c_stop();
        return 0u;
    }

    /* Read data bytes */
    for (i = 0u; i < len; i++)
    {
        /* ACK all bytes except the last one */
        data[i] = i2c_read_byte((i < (len - 1u)) ? 1u : 0u);
    }

    i2c_stop();
    return 1u;
}
