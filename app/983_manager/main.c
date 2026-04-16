/*
 * 983_manager - C port of bios-ver-11.txt for 983HH
 *
 * Flow:
 *   1. Startup delay (matches BIOS script)
 *   2. 983HH port / power bring-up via board_init()
 *   3. PLL init
 *   4. Bit-bang init of bus 1 and shared serializer bus 0
 *   5. Normalize serializer local address back to 0x18 when needed
 *   6. Read DIP switches and execute the matching generated profile table
 */

#include "board.h"
#include "hal_clock.h"
#include "hal_gpio.h"
#include "hal_i2c_bitbang.h"
#include "hal_i2c1_bitbang.h"
#include "hal_uart.h"
#include "lib_boot.h"
#include "profile_data.h"

static uint8 g_serializer_primary_addr = 0x18u;
static uint8 g_last_i2c_ok = 1u;
static uint8 bitbang_probe_addr(uint8 dev_addr);

static void delay_ms_prepll(uint32 ms)
{
    uint32 outer;

    for (outer = 0u; outer < ms; outer++)
    {
        volatile uint32 d = 1500u;
        while (d-- != 0u)
        {
            ;
        }
    }
}

static void delay_ms_postpll(uint32 ms)
{
    uint32 outer;

    for (outer = 0u; outer < ms; outer++)
    {
        volatile uint32 d = 20000u;
        while (d-- != 0u)
        {
            ;
        }
    }
}

#ifdef DEBUG_ENABLED
static void debug_print_banner_postpll(void)
{
    hal_uart_init(BOARD_PCLK_HZ, BOARD_UART_BAUD);

    hal_uart_puts("\n=== 983_manager ===\nBoard: " BOARD_NAME " | FW: v");
    hal_uart_put_hex8((uint8)FW_VERSION_MAJOR);
    hal_uart_putc('.');
    hal_uart_put_hex8((uint8)FW_VERSION_MINOR);
    hal_uart_puts(" | Built: ");
    hal_uart_puts(__DATE__);
    hal_uart_putc(' ');
    hal_uart_puts(__TIME__);
    hal_uart_puts("\nReady.\n");
}
#endif

#ifdef DEBUG_ENABLED
static void debug_put_hex8(uint8 value)
{
    hal_uart_put_hex8(value);
}
#endif

static void debug_put_profile(const char *name)
{
#ifdef DEBUG_ENABLED
    hal_uart_puts("Profile: ");
    hal_uart_puts(name);
    hal_uart_puts("\n");
#else
    (void)name;
#endif
}

#ifdef DEBUG_ENABLED
static void debug_put_i2c_error(void)
{
    hal_uart_puts(" bb");
}
#endif

static uint8 map_dev_addr(uint8 dev_addr)
{
    switch (g_serializer_primary_addr)
    {
    case 0x10u:
        if (dev_addr == 0x18u)
        {
            return 0x10u;
        }
        if (dev_addr == 0x1Au)
        {
            return 0x12u;
        }
        if (dev_addr == 0x1Cu)
        {
            return 0x14u;
        }
        break;

    default:
        break;
    }

    return dev_addr;
}

static void debug_put_failure(uint16 step_index, const init_op_t *op)
{
#ifdef DEBUG_ENABLED
    uint8 mapped_dev = map_dev_addr(op->dev_addr);

    hal_uart_puts("Init failed at step ");
    hal_uart_put_hex8((uint8)(step_index >> 8));
    hal_uart_put_hex8((uint8)step_index);
    hal_uart_puts(" op=");
    hal_uart_put_hex8(op->type);
    hal_uart_puts(" dev=");
    hal_uart_put_hex8(op->dev_addr);
    if (mapped_dev != op->dev_addr)
    {
        hal_uart_puts("->");
        hal_uart_put_hex8(mapped_dev);
    }
    hal_uart_puts(" reg=");
    hal_uart_put_hex8(op->reg_addr);
    hal_uart_puts(" val=");
    hal_uart_put_hex8(op->value);
    debug_put_i2c_error();
    hal_uart_puts("\n");
#else
    (void)step_index;
    (void)op;
#endif
}

static uint8 i2c_reg_write(uint8 dev_addr, uint8 reg_addr, uint8 value)
{
    uint8 buf[2];
    uint8 mapped_dev = map_dev_addr(dev_addr);

    buf[0] = reg_addr;
    buf[1] = value;
    g_last_i2c_ok = hal_i2c_bitbang_write(mapped_dev, buf, 2u);
    return g_last_i2c_ok;
}

static uint8 i2c_reg_read_nonfatal(uint8 dev_addr, uint8 reg_addr)
{
    uint8 value = 0xFFu;
    uint8 mapped_dev = map_dev_addr(dev_addr);
    uint8 ok = 0u;

    if (hal_i2c_bitbang_write(mapped_dev, &reg_addr, 1u) != 0u)
    {
        if (hal_i2c_bitbang_read(mapped_dev, &value, 1u) != 0u)
        {
            ok = 1u;
        }
        else
        {
#ifdef DEBUG_ENABLED
            hal_uart_puts("Read fail ");
            hal_uart_put_hex8(mapped_dev);
            hal_uart_puts(":");
            hal_uart_put_hex8(reg_addr);
            debug_put_i2c_error();
            hal_uart_puts("\n");
#endif
        }
    }
    else
    {
#ifdef DEBUG_ENABLED
        hal_uart_puts("Read fail ");
        hal_uart_put_hex8(mapped_dev);
        hal_uart_puts(":");
        hal_uart_put_hex8(reg_addr);
        debug_put_i2c_error();
        hal_uart_puts("\n");
#endif
    }

    g_last_i2c_ok = ok;

#ifdef DEBUG_ENABLED
    hal_uart_puts("Read ");
    hal_uart_put_hex8(mapped_dev);
    hal_uart_puts(":");
    hal_uart_put_hex8(reg_addr);
    hal_uart_puts(" -> ");
    hal_uart_put_hex8(value);
    hal_uart_puts("\n");
#endif

    return value;
}

static uint8 load_edid(const uint8 *edid, uint16 edid_len)
{
    uint16 idx;

    if ((edid == (const uint8 *)0) || (edid_len == 0u))
    {
        g_last_i2c_ok = 0u;
        return 0u;
    }

    if (i2c_reg_write(0x18u, 0x40u, 0x36u) == 0u)
    {
        return 0u;
    }

    if (i2c_reg_write(0x18u, 0x41u, 0x00u) == 0u)
    {
        return 0u;
    }

    for (idx = 0u; idx < edid_len; idx++)
    {
        if (i2c_reg_write(0x18u, 0x42u, edid[idx]) == 0u)
        {
            return 0u;
        }
    }

    return 1u;
}

static uint8 detect_serializer_primary_addr(void)
{
    if (bitbang_probe_addr(0x18u) != 0u)
    {
        return 0x18u;
    }

    if (bitbang_probe_addr(0x10u) != 0u)
    {
        return 0x10u;
    }

    return 0x18u;
}

static uint8 serializer_force_primary_addr(uint8 new_primary_addr)
{
    uint8 current_addr = g_serializer_primary_addr;
    uint8 buf[2];

    if (current_addr == new_primary_addr)
    {
        return 1u;
    }

    buf[0] = 0x00u;
    buf[1] = (uint8)(((uint32)new_primary_addr << 1) | 0x01u);

    if (hal_i2c_bitbang_write(current_addr, buf, 2u) == 0u)
    {
        return 0u;
    }

    delay_ms_postpll(1u);
    hal_i2c_bitbang_init();

    if (bitbang_probe_addr(new_primary_addr) == 0u)
    {
        return 0u;
    }

    g_serializer_primary_addr = new_primary_addr;
    return 1u;
}

static uint8 bitbang_probe_addr(uint8 dev_addr)
{
    return hal_i2c_bitbang_write(dev_addr, (const uint8 *)0, 0u);
}

#ifdef DEBUG_ENABLED
static uint8 bitbang_reg_read_nonfatal(uint8 dev_addr, uint8 reg_addr, uint8 *value)
{
    if (hal_i2c_bitbang_write(dev_addr, &reg_addr, 1u) == 0u)
    {
        return 0u;
    }

    return hal_i2c_bitbang_read(dev_addr, value, 1u);
}
#endif

static void bitbang_debug_scan(void)
{
#ifdef DEBUG_ENABLED
    uint8 addr;
    uint8 found = 0u;

    hal_uart_puts("BB scan:");
    for (addr = 0x10u; addr <= 0x1Fu; addr++)
    {
        if (bitbang_probe_addr(addr) != 0u)
        {
            hal_uart_puts(" ");
            hal_uart_put_hex8(addr);
            found = 1u;
        }
    }
    if (found == 0u)
    {
        hal_uart_puts(" none");
    }
    hal_uart_puts("\n");
#endif
}

static void bitbang_debug_read_candidate(uint8 dev_addr)
{
#ifdef DEBUG_ENABLED
    uint8 value = 0xFFu;

    if (bitbang_reg_read_nonfatal(dev_addr, 0x05u, &value) != 0u)
    {
        hal_uart_puts("BB ");
        hal_uart_put_hex8(dev_addr);
        hal_uart_puts("[05]=");
        hal_uart_put_hex8(value);
        hal_uart_puts("\n");
    }
    else
    {
        hal_uart_puts("BB ");
        hal_uart_put_hex8(dev_addr);
        hal_uart_puts("[05]=??\n");
    }
#else
    (void)dev_addr;
#endif
}

static uint8 serializer_probe(void)
{
    uint8 value;

    value = i2c_reg_read_nonfatal(g_serializer_primary_addr, 0x05u);

#ifdef DEBUG_ENABLED
    hal_uart_puts("SER@");
    hal_uart_put_hex8(g_serializer_primary_addr);
    hal_uart_puts("[05]=");
    hal_uart_put_hex8(value);
    hal_uart_puts("\n");
#else
    (void)value;
#endif

    return g_last_i2c_ok;
}

static uint8 run_profile(const profile_data_t *profile)
{
    uint16 step_index = 0u;
    uint8 block_idx;

    for (block_idx = 0u; block_idx < profile->block_count; block_idx++)
    {
        const init_op_block_t *block = &profile->blocks[block_idx];
        uint16 op_idx;

        for (op_idx = 0u; op_idx < block->op_count; op_idx++, step_index++)
        {
            const init_op_t *op = &block->ops[op_idx];

            switch (op->type)
            {
            case INIT_OP_WRITE:
                if (i2c_reg_write(op->dev_addr, op->reg_addr, op->value) == 0u)
                {
                    debug_put_failure(step_index, op);
                    return 0u;
                }
                break;

            case INIT_OP_READ:
                (void)i2c_reg_read_nonfatal(op->dev_addr, op->reg_addr);
                break;

            case INIT_OP_DELAY_MS:
                delay_ms_postpll(op->delay_ms);
                break;

            case INIT_OP_LOAD_EDID:
                if (load_edid(profile->edid, profile->edid_len) == 0u)
                {
                    debug_put_failure(step_index, op);
                    return 0u;
                }
                break;

            default:
                debug_put_failure(step_index, op);
                return 0u;
            }
        }
    }

    return 1u;
}

int main(void)
{
    const profile_data_t *profile;
    uint8 dip_raw;
    uint8 dip_on_mask;

    delay_ms_prepll(2000u);
    board_init();

    hal_clock_init((void (*)(void))0);

#ifdef DEBUG_ENABLED
    debug_print_banner_postpll();
#endif

#ifdef DEBUG_ENABLED
    hal_uart_puts("PLL done, CPU=80MHz\n");
#endif

    /* BIOS also initializes bus 1 even though this init path uses bus 0 only. */
    hal_i2c1_bitbang_init();

    /*
     * Diagnostic pre-scan on bus 0 using the proven GPIO bit-bang path.
     * This bypasses RIIC0 completely and tells us what slave addresses the
     * RH850 sees on the shared serializer bus before we switch the pins to AF2.
     */
    hal_i2c_bitbang_init();
    bitbang_debug_scan();
    bitbang_debug_read_candidate(0x18u);
    bitbang_debug_read_candidate(0x10u);
    g_serializer_primary_addr = detect_serializer_primary_addr();

#ifdef DEBUG_ENABLED
    hal_uart_puts("Serializer primary addr=");
    hal_uart_put_hex8(g_serializer_primary_addr);
    hal_uart_puts("\n");
#endif

    if (g_serializer_primary_addr != 0x18u)
    {
#ifdef DEBUG_ENABLED
        hal_uart_puts("Force serializer addr ->18\n");
#endif
        if (serializer_force_primary_addr(0x18u) != 0u)
        {
#ifdef DEBUG_ENABLED
            hal_uart_puts("Serializer addr forced to ");
            hal_uart_put_hex8(g_serializer_primary_addr);
            hal_uart_puts("\n");
#endif
        }
        else
        {
#ifdef DEBUG_ENABLED
            hal_uart_puts("Serializer addr override failed, keeping ");
            hal_uart_put_hex8(g_serializer_primary_addr);
            hal_uart_puts("\n");
#endif
        }
    }

    dip_raw = hal_gpio_read_dip();
    dip_on_mask = (uint8)(~dip_raw);

#ifdef DEBUG_ENABLED
    hal_uart_puts("DIP raw=");
    debug_put_hex8(dip_raw);
    hal_uart_puts(" on_mask=");
    debug_put_hex8(dip_on_mask);
    hal_uart_puts("\n");
#endif

    profile = profile_select(dip_on_mask);
    if (profile == (const profile_data_t *)0)
    {
#ifdef DEBUG_ENABLED
        hal_uart_puts("Unsupported DIP combination\n");
#endif
        for (;;)
        {
            ;
        }
    }

    debug_put_profile(profile->name);

    if (serializer_probe() == 0u)
    {
#ifdef DEBUG_ENABLED
        hal_uart_puts("Serializer probe failed\n");
#endif
        for (;;)
        {
            ;
        }
    }

    if (run_profile(profile) == 0u)
    {
        for (;;)
        {
            ;
        }
    }

#ifdef DEBUG_ENABLED
    hal_uart_puts("983_manager init complete\n");
#endif

    for (;;)
    {
        ;
    }

}
