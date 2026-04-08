# CLAUDE.md — Project Context for Claude Code

## Project Overview

Bare-metal C firmware for Renesas RH850/F1KM-S1 MCU (R7F7016863), supporting
multiple boards. Built with CC-RH compiler and GNU Make. No RTOS — pure
bare-metal with interrupt-driven peripherals.

**Target use case:** Automotive remote displays with built-in RH850 MCU,
providing diagnostics, monitoring, I2C-based communication, and future
firmware update / bootloader mechanisms.

## Supported Boards

| Board | Description | Apps |
|-------|-------------|------|
| `983HH` | Dev board with LED, DIP switches, PCF8574A I2C expander | blink_led, mirror_dip, i2c_bitbang, i2c_master_pcf8574, i2c_slave |
| `REMOTE_DISP` | Automotive remote display (15.6" LCD, FPGA, FPD-Link deserializer, NTC) | display_manager |

## Build System

```bash
# 983HH examples
make BOARD=983HH APP=blink_led
make BOARD=983HH APP=i2c_slave DEBUG=on VERSION=01.10

# REMOTE_DISP display manager
make BOARD=REMOTE_DISP APP=display_manager
make BOARD=REMOTE_DISP APP=display_manager DEBUG=on VERSION=01.00

# MISRA static analysis
make misra          # Full report
make misra-count    # Summary by rule

# Utilities
make info                                          # Show config
make BOARD=983HH APP=i2c_slave size                # Link size vs 256KB limit
make BOARD=983HH APP=i2c_slave clean               # Clean specific app
make clean-all                                     # Clean all boards/apps
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
- **No signed types in dvf.h:** `int16` and `int32` are defined in `hal_adc.h`.
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
├── board/
│   ├── 983HH/                 Dev board: LED, DIP switches, PCF8574A
│   │   ├── board.h            Pin/clock defines, BOARD_NAME, I2C speed (100kHz)
│   │   ├── board_init.c       LED + DIP switch GPIO init
│   │   └── board_vectors.h    INTC2 addresses: RIIC0 ch76-79, OSTM0 ch84
│   └── REMOTE_DISP/           Automotive remote display
│       ├── board.h            Power pins, PCL, NTC params, I2C speed (400kHz)
│       ├── board_init.c       Power-up/down sequences, UG3V3_EN self-hold
│       └── board_vectors.h    Same interrupt channels as 983HH
├── hal/
│   ├── hal_clock.c/.h         PLL1 init, prot_write0/1 helpers
│   ├── hal_gpio.c/.h          GPIO ports 0,8,9,10,11, AP0 read
│   ├── hal_uart.c/.h          RLIN32 UART: blocking + non-blocking (ring buffer)
│   ├── hal_adc.c/.h           ADCA0: polling single-channel 12-bit ADC
│   ├── hal_riic_slave.c/.h    RIIC0 slave: interrupt-driven, 16-bit sub-addressing
│   ├── hal_riic_master.c/.h   RIIC0 master: polling (bus 0, P10_2/P10_3)
│   ├── hal_riic1_master.c/.h  RIIC1 master: polling (reserved, HW not on P8)
│   ├── hal_i2c_bitbang.c/.h   Bit-bang I2C bus 0 (P10_2/P10_3, 983HH)
│   ├── hal_i2c1_bitbang.c/.h  Bit-bang I2C bus 1 (P8_0/P8_1, REMOTE_DISP)
│   └── hal_timer.c/.h         OSTM0 interval timer (1ms)
├── lib/
│   ├── lib_boot.c/.h          Standard boot banner (conditional on DEBUG=on)
│   ├── lib_debug.h            DBG_PUTS/DBG_HEX8 → non-blocking ring buffer
│   └── lib_ringbuf.c/.h       Lock-free SPSC ring buffer (512 bytes)
├── app/
│   ├── blink_led/main.c       LED blink (983HH, no PLL)
│   ├── mirror_dip/main.c      DIP switch → LED (983HH, no PLL)
│   ├── i2c_bitbang/main.c     PCF8574A via bit-bang I2C (983HH, no PLL)
│   ├── i2c_master_pcf8574/main.c  PCF8574A via HW RIIC0 (983HH, PLL)
│   ├── i2c_slave/main.c       I2C slave 0x50, register map (983HH, PLL)
│   └── display_manager/main.c Remote display controller (REMOTE_DISP, PLL)
└── docs/
    ├── i2c_register_map.md    I2C slave protocol spec (EEPROM-style, 64K addr)
    ├── clock_system.md        PLL setup reference
    ├── riic_bringup.md        RIIC0 critical learnings (PBDC, PODC, IER)
    ├── pin_functions.md       Pin mux reference
    ├── misra-c2025-compliance-plan.md   MISRA phases (all complete)
    ├── misra-baseline-report.md         209 → 77 (0 fixable remaining)
    └── misra_deviations.md    10 formal deviation records (DEV-001 to DEV-010)
```

## Branches

- **`main`**: Stable tested baseline.
- **`feature/pcl-power-control`**: PCL-based display power control (tested, ready to merge).
- **`misrac-2025`**: Historical — MISRA compliance work, merged to main.

## I2C Slave Protocol (16-bit sub-addressing)

EEPROM-style (24C256 compatible). Auto-increment, wraps at 0xFFFF.

```
Write: [0x50+W] [addr_hi] [addr_lo] [data0] [data1] ...
Read:  [0x50+W] [addr_hi] [addr_lo] [0x50+R] [data0] ...
```

Register map (see `docs/i2c_register_map.md` for full spec):

| Range | Page | Implemented registers |
|-------|------|-----------------------|
| `0x0000-0x00FF` | Device Info (RO) | FW version BCD at 0x0000-0x0001 |
| `0x0100-0x01FF` | Status (RO) | Display state at 0x0100, DIP switches (983HH) |
| `0x0200-0x02FF` | Control (RW) | Display power at 0x0200, LED (983HH) |
| `0x0300-0x03FF` | Debug (RW) | Scan cmd 0x0300, status 0x0301, I2C log 0x0302 |
| `0x1000-0x1FFF` | Diagnostics (RO) | Backlight NTC: raw ADC 0x1000-1001, temp 0x1002-1003 |
| `0xF000-0xFEFF` | Firmware Update | (future: image staging) |
| `0xFF00-0xFFFF` | Bootloader | (future: update trigger, CRC) |

### Temperature register format

Signed 16-bit, 0.1 degC resolution, big-endian:
- `0x00FD` = 253 = 25.3 C
- `0xFF9C` = -100 = -10.0 C

### Pi4 test commands

```bash
# Firmware version
i2ctransfer -y 1 w2@0x50 0x00 0x00 r2@0x50

# 983HH: LED ON / OFF
i2ctransfer -y 1 w3@0x50 0x02 0x00 0x01
i2ctransfer -y 1 w3@0x50 0x02 0x00 0x00

# 983HH: DIP switches
i2ctransfer -y 1 w2@0x50 0x01 0x00 r1@0x50

# REMOTE_DISP: Display power OFF / ON
i2ctransfer -y 1 w3@0x50 0x02 0x00 0x00
i2ctransfer -y 1 w3@0x50 0x02 0x00 0x01

# REMOTE_DISP: Read display state (0=OFF, 1=ON)
i2ctransfer -y 1 w2@0x50 0x01 0x00 r1@0x50

# REMOTE_DISP: Backlight temperature (raw + degC in one read)
i2ctransfer -y 1 w2@0x50 0x10 0x00 r4@0x50

# REMOTE_DISP: I2C1 bus scan (prints i2cdetect table on UART)
i2ctransfer -y 1 w3@0x50 0x03 0x00 0x01

# REMOTE_DISP: Disable/enable I2C slave transaction debug on UART
i2ctransfer -y 1 w3@0x50 0x03 0x02 0x00    # disable
i2ctransfer -y 1 w3@0x50 0x03 0x02 0x01    # enable
```

## ADC / NTC Temperature Monitoring (REMOTE_DISP)

- **ADC:** ADCA0 channel 0 (ANI00 = AP0_0), 12-bit, polling mode
- **Circuit:** +3.3V --- 3.3K pullup --- 100R --- AP0_0 --- NTC --- GND
- **NTC:** NTCS0603E3103FHT (10K@25C, Beta=3960)
- **Sampling:** Every 100ms from OSTM0 timer callback
- **Conversion:** Integer-only Beta equation with piecewise ln() approximation
- **Register values from Smart Configurator** (see `tmp-sample-code/renesas-smart-config/`)

## I2C1 Bus Scan (REMOTE_DISP)

- **Bus:** P8_0 (SDA1), P8_1 (SCL1), **bit-banged** (RIIC1 HW not routed to these pins on 100-pin variant)
- **Trigger:** Write 0x01 to register 0x0300 (from Pi4 or any I2C master)
- **Output:** i2cdetect-style table on UART debug terminal
- **ISR debug auto-suppressed** during scan for clean output, restored after
- **Devices found on REMOTE_DISP:** 0x30 (DS90UB9xx deserializer), 0x6B (TBD)

## PCL Display Power Control (REMOTE_DISP)

AP0_4 (PCL) controls display power state:
- **PCL LOW** = display ON
- **PCL HIGH** = display OFF

**Self-hold:** P10_5 (UG3V3_EN) driven HIGH as the very first operation
in `main()`. PCL from vehicle may be a short pulse — MCU latches its
own 3.3V supply before the pulse ends.

**State machine** in main loop with 50ms debounce (sampled in 1ms timer):

Two sources control display power with priority logic:
- **PCL** (AP0_4): hardware signal from vehicle (HIGH=OFF, always wins)
- **I2C** (reg 0x0200): software command from head-unit (0x00=OFF, 0x01=ON)

```
Display ON  = (PCL=LOW) AND (I2C cmd=ON)
Display OFF = (PCL=HIGH) OR (I2C cmd=OFF)
```

```
DISP_OFF ──(want_on)──► DISP_ON
    ▲                       │
    └───(!want_on)──────────┘
```

**I2C display control (register 0x0200):**
```bash
i2ctransfer -y 1 w3@0x50 0x02 0x00 0x00    # display OFF
i2ctransfer -y 1 w3@0x50 0x02 0x00 0x01    # display ON
i2ctransfer -y 1 w2@0x50 0x01 0x00 r1@0x50 # read state (0=OFF, 1=ON)
```

**Power cycle (verified on hardware):**
```
Video pipeline: FPD-Link → Deserializer → FPGA → LCD + Backlight

Power-down (board_power_down):
  1. LCD shutdown (assert resets)
  2. Backlight disable
  3. FPGA reset assert + program low
  4. FPGA power rails disable
  (Deserializer + main power stay running)

Power-on (board_power_on):
  1. FPGA power rails enable + 5ms
  2. FPGA program + reset release (5ms each)
  3. FPGA config settle delay (50ms)
  4. SPI CS init
  5. Backlight enable
  6. LCD reset sequence (10ms + 6ms + 5ms + 95ms)
```

**Key findings from power cycle testing:**
- Deserializer must stay powered — re-init requires I2C (future work)
- `port_init()` must NOT re-run on power-on — drives GPIO LOW while
  already outputs, glitches FPGA/LCD control signals
- Cold boot uses `board_init()`, re-power uses `board_power_on()`
- Main power can be disabled during power-down — MCU survives on
  UG3V3_EN self-hold

**I2C register:** `0x0100` = display state (0=OFF, 1=ON).
I2C slave stays active in both states.

## Non-Blocking Debug Architecture

```
I2C ISR → DBG_PUTS() → ring buffer (~1 us) → OSTM0 ISR (1ms) → UART TX
```

- ISR debug prints push to lock-free ring buffer (zero I2C clock-stretching)
- OSTM0 timer fires every 1 ms, drains up to 8 bytes to UART per tick
- Boot messages use blocking UART (before timer is running)
- Entire debug system compiles to nothing when `DEBUG=off`
- Boot banner uses `BOARD_NAME` from board.h for correct board identification

## REMOTE_DISP Power-Up Sequence

Translated from BIOS init script (`tmp-sample-code/remote-disp-init/`):

1. GPIO port directions (exclude P0_13/P0_14 — UART pins)
2. Main power: 5V, 3.3V, PMIC enable + 5ms
3. FPGA power: 1.1V, 1.35V, 1.2V, 2.5V + 5ms
4. FPGA program + reset release (5ms each)
5. Deserializer power: 1.8V, 1.15V, DCDC reset + 5ms
6. SPI chip select init
7. Backlight enable
8. LCD reset sequence (10ms + 6ms + 5ms + 95ms)

## MISRA C:2025 Compliance

**Status: Complete.** 209 baseline → 132 fixed → 77 documented deviations → 0 fixable.

Key references:
- `docs/misra-c2025-compliance-plan.md` — 5-phase plan (all phases done)
- `docs/misra-baseline-report.md` — Before/after violation counts
- `docs/misra_deviations.md` — 10 formal deviation records
- `make misra-count` — Quick check (expect 77, all deviations)

MISRA-safe patterns used throughout:
- Explicit boolean comparisons: `if ((val & MASK) != 0u)` not `if (val & MASK)`
- Compound statements: `{ }` on all if/while/for bodies
- No parameter modification: use local copy
- Void cast unused returns: `(void)func()`
- No side effects in `&&`/`||`: restructured to `while (t != 0u) { if (cond) break; t--; }`
- Function pointer null check: `!= (void *)0` (not `NULL`)

## Hardware Reference

### 983HH Board

| Item | Value |
|------|-------|
| MCU | R7F7016863 (F1KM-S1, 100-pin LQFP) |
| Clock | 16 MHz MainOSC → PLL1 → 80 MHz CPU, 40 MHz peripherals |
| RAM | 0xFEBF0000, 32 KB (stack 4 KB at top) |
| Flash | 0x00000000, 512 KB |
| UART | RLIN32: P0_13 (RX), P0_14 (TX), AF1, 115200 baud |
| I2C | RIIC0: P10_2 (SDA), P10_3 (SCL), AF2, ~100 kHz |
| LED | P9_6 (active high) |
| DIP | AP0_7 through AP0_14 (8 switches) |
| Timer | OSTM0: interrupt ch84, INTC2 at 0xFFFFB0A8 |

### REMOTE_DISP Board

| Item | Value |
|------|-------|
| MCU | R7F7016863 (same as 983HH) |
| Display | 15.6" LCD (POC_9090), FPD-Link deserializer (DS90UB9xx) |
| FPGA | On-board, programmed via P8_6, reset via P8_5 |
| I2C0 | RIIC0 slave: P10_2 (SDA), P10_3 (SCL), 400 kHz fast mode |
| NTC | NTCS0603E3103FHT on AP0_0, 3.3K pullup, Beta=3960 |
| Backlight | VLED_ON on P10_11 |
| PCL | AP0_4 input (LOW=display ON, HIGH=display OFF) |
| UG3V3_EN | P10_5 output (MCU 3.3V self-hold, assert HIGH at boot) |
| Power | 7 supply rails with sequenced enable (see board_init.c) |
| UART | Same as 983HH: P0_13/P0_14, 115200 baud |

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

8. **Board port init must not clobber UART pins**: REMOTE_DISP port_init()
   originally configured P0_14 (UART TX) as GPIO, killing debug output.
   Exclude UART pins (P0_13, P0_14) from port direction masks.

9. **BIOS script drives pin HIGH before configuring as output**: The pattern
   `P xx 20 mask` then `P xx 82 mask 0` sets level first, then direction.
   This avoids glitching power rails LOW during output configuration.

10. **ADC first reading may be zero**: ADCA0 needs a settling period after init.
    The first `hal_adc_read()` at boot returns 0. Timer-based readings (100ms+
    after init) are stable and accurate.

11. **UG3V3_EN self-hold must be first operation**: P10_5 must be driven HIGH
    before anything else in `main()`. PCL from vehicle is a short pulse —
    MCU loses power if self-hold isn't asserted before the pulse ends.

12. **port_init() must NOT re-run on display re-power**: Drives all GPIO LOW
    while pins are already outputs, glitching FPGA/LCD signals. Use separate
    `board_init()` (cold boot) and `board_power_on()` (re-power) paths.

13. **P10_5 must be excluded from port_init clear mask**: Original mask 0x94B3
    includes bit 5 which drives UG3V3_EN LOW, killing MCU power. Use 0x9493.

14. **Deserializer must stay powered during display power cycle**: FPGA and LCD
    can be power-cycled, but deserializer re-init requires I2C communication
    (not yet implemented). Keep deser powered for now.

15. **FPGA needs 50ms settle after re-power before LCD init**: After FPGA power
    rails + program + reset release, the FPGA loads its bitstream from flash.
    Without a settle delay, LCD reset runs before FPGA can drive pixels —
    display blinks but stays black. 50ms is sufficient.

16. **SPI CS must be re-initialized on power-on**: `spi_cs_init()` was only in
    cold boot path. Missing from `board_power_on()` caused first ON after OFF
    to fail.

17. **RIIC1 hardware not routed to P8_0/P8_1 on 100-pin variant**: RIIC1 HW
    master produced no ACKs on these pins. The BIOS uses software (bit-bang)
    I2C on P8_0/P8_1 — use `hal_i2c1_bitbang` instead of `hal_riic1_master`.

18. **ISR debug interleaves with blocking UART**: Ring buffer drain in timer ISR
    outputs bytes between blocking `hal_uart_puts()` calls. Fix: suppress ISR
    debug (`g_riic_slave_dbg_en=0`), drain buffer, then print.

## Future Extensions (Planned)

- **Deserializer I2C re-init**: Use hal_i2c1_bitbang to configure DS90UB9xx
  (at 0x30) after power cycle — currently deser must stay powered
- **Identify device at 0x6B** on I2C1 bus (found via scan)
- **Main message loop** with timer-driven background workers (`lib_msgloop.c/.h`)
- **DLT/DLS diagnostics**: Binary runtime trace on UART (after text boot banner)
- **SPI HAL** (`hal_spi.c/.h`): CSIH1 for REMOTE_DISP FPGA communication
- **Additional ADC channels**: Supply voltage monitoring
- **I2C firmware update**: Image staging at 0xF000, bootloader control at 0xFF00
- **A/B failsafe firmware upgrade**: Bootloader with CRC validation

## Adding New Code

**New board:** Create `board/<NAME>/board.h` (must define `BOARD_NAME`),
`board_init.c`, `board_vectors.h`. Build with `make BOARD=<NAME> APP=<app>`.

**New HAL module:** Create `hal/hal_<name>.c/.h`, add ISR to `boot.asm` EIINTTBL
if needed, add INTC2 address to `board_vectors.h`.

**New register page:** Add address constants to app's `main.c`, handle in
`on_read`/`on_write` callbacks, update `docs/i2c_register_map.md`.

**New app:** Create `app/<name>/main.c`, include `lib_boot.h`, add `BOOT_BANNER()`.
Build with `make BOARD=<BOARD> APP=<name>`.

**After any code change:** Run `make misra-count` and verify count is reasonable.
