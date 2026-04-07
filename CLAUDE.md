# CLAUDE.md — Project Context for Claude Code

## Project Overview

Bare-metal C firmware for Renesas RH850/F1KM-S1 MCU (R7F7016863) on the
983HH board. Built with CC-RH compiler and GNU Make. No RTOS — pure
bare-metal with interrupt-driven peripherals.

**Target use case:** Automotive remote displays with built-in RH850 MCU,
evolving toward diagnostics, monitoring, I2C-based firmware updates, and
bootloader failsafe mechanisms.

## Build System

```bash
# Basic build
make BOARD=983HH APP=blink_led

# With debug UART output
make BOARD=983HH APP=i2c_slave DEBUG=on

# With firmware version
make BOARD=983HH APP=i2c_slave VERSION=01.10

# MISRA static analysis
make misra          # Full report
make misra-count    # Summary by rule

# Show config
make info

# Clean (must specify APP)
make BOARD=983HH APP=i2c_slave clean
```

- **Compiler:** CC-RH V2.07.00 at `/usr/local/Renesas/CC-RH/V2.07.00/`
- **WSL workaround:** CC-RH is 32-bit and can't access `/mnt/c/`. Build happens in `/tmp/`.
- **Output:** `output/<BOARD>/<APP>/release/` and `output/<BOARD>/<APP>/debug/`
- **Flash tool:** `flashrh850.sh` on Raspberry Pi via `/dev/ttyS0`

## CC-RH Compiler Constraints

- **C89/C90 only:** Variable declarations MUST be at the top of each block,
  before any executable statements. Mixing causes `E0520268`.
- **No `NULL`:** Use `(void *)0` instead. CC-RH's `stddef.h` has `NULL` but
  it's not included by default.
- **Vendor types:** `uint32`, `uint16`, `uint8` from `dr7f701686.dvf.h`
  (not `stdint.h`). These are `unsigned long`, `unsigned short`, `unsigned char`.
- **Compiler extensions:** `#pragma interrupt(...)`, `__EI()`, `__nop()` are
  CC-RH specific and required for RH850 interrupt handling.
- **RIIC register access:** Use `.UINT32` (32-bit) access with values in the
  low byte. `.UINT8[]` does NOT work correctly for RIIC registers.

## Project Structure

```
rh850-baremetal-demo/
├── Makefile                    Single Makefile: BOARD/APP/DEBUG/VERSION
├── device/dr7f701686.dvf.h    Renesas device header (committed, vendor license)
├── startup/
│   ├── boot.asm               Exception vectors, EIINTTBL (512 slots), register init
│   └── cstart.asm             Stack (4KB), BSS/data init, FPU, jump to main
├── board/983HH/
│   ├── board.h                Pin/clock defines + board_init() declaration
│   ├── board_init.c           LED + DIP switch GPIO init
│   └── board_vectors.h        INTC2 addresses: RIIC0 ch76-79, OSTM0 ch84
├── hal/
│   ├── hal_clock.c/.h         PLL1 init, prot_write0/1 helpers
│   ├── hal_gpio.c/.h          GPIO with PSR_SET/PSR_CLR atomic macros
│   ├── hal_uart.c/.h          RLIN32 UART: blocking + non-blocking (ring buffer)
│   ├── hal_riic_slave.c/.h    RIIC0 slave: interrupt-driven, 16-bit sub-addressing
│   ├── hal_riic_master.c/.h   RIIC0 master: polling
│   ├── hal_i2c_bitbang.c/.h   Bit-bang I2C with bus recovery (9-clock SCL)
│   └── hal_timer.c/.h         OSTM0 interval timer (1ms for ring buffer drain)
├── lib/
│   ├── lib_boot.c/.h          Standard boot banner (conditional on DEBUG=on)
│   ├── lib_debug.h            DBG_PUTS/DBG_HEX8 → non-blocking ring buffer
│   └── lib_ringbuf.c/.h       Lock-free SPSC ring buffer (512 bytes)
├── app/
│   ├── blink_led/main.c       LED blink (no PLL)
│   ├── mirror_dip/main.c      DIP switch → LED (no PLL)
│   ├── i2c_bitbang/main.c     PCF8574A via bit-bang I2C (no PLL)
│   ├── i2c_master_pcf8574/main.c  PCF8574A via HW RIIC0 (PLL required)
│   └── i2c_slave/main.c       I2C slave 0x50, 16-bit register map (PLL required)
└── docs/
    ├── i2c_register_map.md    I2C slave protocol spec (EEPROM-style, 64K address space)
    ├── clock_system.md        PLL setup reference
    ├── riic_bringup.md        RIIC0 critical learnings (PBDC, PODC, IER)
    ├── pin_functions.md       Pin mux reference
    ├── misra-c2025-compliance-plan.md   MISRA phases (all complete)
    ├── misra-baseline-report.md         209 → 77 (0 fixable remaining)
    └── misra_deviations.md    10 formal deviation records (DEV-001 to DEV-010)
```

## Branches

- **`main`**: Stable, tested baseline. All 5 apps verified on 983HH hardware.
- **`misrac-2025`**: MISRA C:2025 compliance work. All 5 phases complete.
  Pending: hardware regression test, then merge to main.

## I2C Slave Protocol (16-bit sub-addressing)

EEPROM-style (24C256 compatible). Auto-increment, wraps at 0xFFFF.

```
Write: [0x50+W] [addr_hi] [addr_lo] [data0] [data1] ...
Read:  [0x50+W] [addr_hi] [addr_lo] [0x50+R] [data0] ...
```

Register map (see `docs/i2c_register_map.md` for full spec):

| Range | Page | Current registers |
|-------|------|-------------------|
| `0x0000-0x00FF` | Device Info (RO) | FW version BCD at 0x0000-0x0001 |
| `0x0100-0x01FF` | Status (RO) | DIP switches at 0x0100 |
| `0x0200-0x02FF` | Control (RW) | LED at 0x0200 |
| `0x0300-0x03FF` | Configuration | (future) |
| `0x1000-0x1FFF` | Diagnostics | (future: temp, voltage, error counters) |
| `0xF000-0xFEFF` | Firmware Update | (future: image staging) |
| `0xFF00-0xFFFF` | Bootloader | (future: update trigger, CRC) |

Pi4 test commands:
```bash
i2ctransfer -y 1 w2@0x50 0x00 0x00 r2@0x50     # FW version
i2ctransfer -y 1 w3@0x50 0x02 0x00 0x01         # LED ON
i2ctransfer -y 1 w3@0x50 0x02 0x00 0x00         # LED OFF
i2ctransfer -y 1 w2@0x50 0x01 0x00 r1@0x50      # DIP switches
```

## Non-Blocking Debug Architecture

```
I2C ISR → DBG_PUTS() → ring buffer (~1 us) → OSTM0 ISR (1ms) → UART TX
```

- ISR debug prints push to lock-free ring buffer (zero I2C clock-stretching)
- OSTM0 timer fires every 1 ms, drains up to 8 bytes to UART per tick
- Boot messages use blocking UART (before timer is running)
- Entire debug system compiles to nothing when `DEBUG=off`

## MISRA C:2025 Compliance

**Status: Complete.** 209 baseline → 132 fixed → 77 documented deviations → 0 fixable.

Key references:
- `docs/misra-c2025-compliance-plan.md` — 5-phase plan (all phases done)
- `docs/misra-baseline-report.md` — Before/after violation counts
- `docs/misra_deviations.md` — 10 formal deviation records
- `make misra-count` — Quick check: should show 77 (all deviations)

MISRA-safe patterns used throughout:
- Explicit boolean comparisons: `if ((val & MASK) != 0u)` not `if (val & MASK)`
- Compound statements: `{ }` on all if/while/for bodies
- No parameter modification: use local copy
- Void cast unused returns: `(void)func()`
- No side effects in `&&`/`||`: restructured to `while (t != 0u) { if (cond) break; t--; }`

## Hardware Reference (983HH Board)

| Item | Value |
|------|-------|
| MCU | R7F7016863 (F1KM-S1, 100-pin LQFP) |
| Clock | 16 MHz MainOSC → PLL1 → 80 MHz CPU, 40 MHz peripherals |
| RAM | 0xFEBF0000, 32 KB (stack 4 KB at top) |
| Flash | 0x00000000, 512 KB |
| UART | RLIN32: P0_13 (RX), P0_14 (TX), AF1, 115200 baud |
| I2C | RIIC0: P10_2 (SDA), P10_3 (SCL), AF2, ~94 kHz |
| LED | P9_6 (active high) |
| DIP | AP0_7 through AP0_14 (8 switches) |
| Timer | OSTM0: interrupt ch84, INTC2 at 0xFFFFB0A8 |

## Critical Learnings (Don't Repeat These Mistakes)

1. **RIIC needs PBDC=1**: Without port bidirectional control, RIIC state machine
   never detects bus-free. START never completes.

2. **RIIC needs IER=0xFC even for polling**: SR2 status flags (TDRE, TEND, etc.)
   are ONLY set when corresponding IER bits are enabled.

3. **UART must reinit after PLL switch**: LIN clock changes immediately when
   CPUCLKS switches to PLL. Call `uart_reinit()` right after clock switch.

4. **Stack size 4 KB minimum**: Default 512 bytes causes silent BSS corruption
   when ISRs call UART functions.

5. **BSS may not be zeroed**: Always explicitly initialize globals in `main()`.

6. **I2C bus recovery on init**: Quick power cycle can leave PCF8574A mid-byte.
   Clock SCL 9 times to flush stuck slave before first transaction.

7. **EIINTTBL not auto-patched**: CC-RH `#pragma interrupt(channel=N)` does NOT
   populate the vector table in standalone Makefile builds. ISR addresses must be
   manually placed in `boot.asm`.

## Future Extensions (Planned)

- **Main message loop** with timer-driven background workers (`lib_msgloop.c/.h`)
- **DLT/DLS diagnostics**: Binary runtime trace on UART (after text boot banner)
- **Temperature/voltage monitoring**: ADC readings in diagnostics register page
- **I2C firmware update**: Image staging at 0xF000, bootloader control at 0xFF00
- **A/B failsafe firmware upgrade**: Bootloader with CRC validation
- **Additional boards**: New `board/<name>/` directories

## Adding New Code

**New HAL module:** Create `hal/hal_<name>.c/.h`, add ISR to `boot.asm` EIINTTBL
if needed, add INTC2 address to `board_vectors.h`.

**New register page:** Add address constants to app's `main.c`, handle in
`on_read`/`on_write` callbacks, update `docs/i2c_register_map.md`.

**New app:** Create `app/<name>/main.c`, include `lib_boot.h`, add `BOOT_BANNER()`.
Build with `make BOARD=983HH APP=<name>`.

**After any code change:** Run `make misra-count` and verify count stays at 77.
