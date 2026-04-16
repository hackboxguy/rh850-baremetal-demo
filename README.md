# RH850/F1KM-S1 Bare-Metal Demo

Modular bare-metal C examples for the Renesas RH850/F1KM-S1 MCU (R7F7016863),
built with the CC-RH compiler and GNU Make.

New developer on WSL2? Start with [SETUP.md](SETUP.md) before using the quick
start commands below.

## Target Hardware

| Item | Value |
|------|-------|
| MCU | R7F7016863 (RH850/F1KM-S1, 100-pin LQFP) |
| Board | 983HH |
| Compiler | CC-RH V2.07.00 |
| Oscillator | 16 MHz MainOSC, 80 MHz CPU (with PLL) |
| Debug UART | RLIN32 on P0_13/P0_14, 115200 baud |
| I2C | RIIC0 on P10_2 (SDA) / P10_3 (SCL) |
| Serializer PDB | P9_6 (active high) |
| DIP switches | AP0_7 through AP0_14 |

## Quick Start

```bash
# Build the 983 serializer manager
make BOARD=983HH APP=983_manager

# Build 983_manager with debug output
make BOARD=983HH APP=983_manager DEBUG=on

# Verify generated 983_manager assets and profile equivalence
make check-983-manager

# Refresh the committed built-in 983_manager golden baseline
make refresh-983-manager-golden

# Flash the binary (on Raspberry Pi)
./micropanel/bin/flashrh850.sh \
    --bios-autorun=output/983HH/983_manager/release/983HH_983_manager.bin \
    --npj=/home/pi/micropanel/share/sp6bins/config/983HH.npj
```

## Available Applications

| APP | Description | Requires PLL |
|-----|-------------|:---:|
| `983_manager` | C port of `bios-ver-11.txt` for 983HH serializer/deserializer bring-up | Yes |
| `blink_led` | Legacy demo for older LED-wired 983HH variants; not for populated serializer boards | No |
| `mirror_dip` | Legacy demo for older LED-wired 983HH variants; not for populated serializer boards | No |
| `i2c_bitbang` | Bit-banged I2C to PCF8574A | No |
| `i2c_master_pcf8574` | HW RIIC0 master to PCF8574A | Yes |
| `i2c_slave` | HW RIIC0 slave with register map | Yes |

## Project Structure

```
rh850-baremetal-demo/
├── Makefile                 Top-level: make BOARD=983HH APP=<app>
├── device/                  MCU device header (dr7f701686.dvf.h)
├── startup/                 Boot code (exception vectors, C runtime)
│   ├── boot.asm             Reset vector, EIINTTBL, register init
│   └── cstart.asm           Stack, BSS/data init, FPU, jump to main
├── board/983HH/             Board-specific configuration
│   ├── board.h              Pin assignments, clock frequencies, 983HH power pins
│   ├── board_init.c         983HH cold-boot port/power sequence (1V8, 1V15, PDB)
│   └── board_vectors.h      INTC2 interrupt channel assignments
├── hal/                     Hardware Abstraction Layer
│   ├── hal_clock.c/h        PLL init, protected write helpers
│   ├── hal_gpio.c/h         GPIO read/write, PSR atomic macros
│   ├── hal_uart.c/h         RLIN3 UART (polling TX)
│   ├── hal_riic_slave.c/h   RIIC0 slave (interrupt-driven, 16-bit addr)
│   ├── hal_riic_master.c/h  RIIC0 master (polling)
│   ├── hal_i2c_bitbang.c/h  Bit-banged I2C (GPIO-based)
│   └── hal_timer.c/h        OSTM0 interval timer
├── lib/                     Portable libraries
│   ├── lib_debug.h          Debug macros (non-blocking, compile-time on/off)
│   └── lib_ringbuf.c/h      Lock-free SPSC ring buffer
├── app/                     Application examples
│   ├── 983_manager/         BIOS-script port for 983HH display bring-up
│   │   ├── main.c           Runtime: DIP decode, serializer address force, init execution
│   │   ├── profile_data.h   Generated block/EDID metadata
│   │   ├── profile_data.c   Generated shared init blocks + profile table
│   │   ├── panels.json      Versioned profile manifest
│   │   ├── golden_reference.json  Committed built-in baseline hashes
│   │   └── edid/            Versioned EDID binary assets
│   ├── blink_led/main.c
│   ├── mirror_dip/main.c
│   ├── i2c_bitbang/main.c
│   ├── i2c_master_pcf8574/main.c
│   └── i2c_slave/main.c
├── tools/
│   ├── gen_983_manager_profiles.py  Generate profile_data.c/.h from BIOS + manifest
│   ├── add_983_panel.py             Import a new EDID asset into panels.json
│   └── check_983_manager.py         Verify EDIDs, block expansion, golden baseline
└── docs/                    Reference documentation
    ├── 983_manager.md       983HH display manager flow and profile notes
    └── i2c_register_map.md  I2C slave protocol and register spec
```

## Adding a New Board

1. Create `board/<BOARD_NAME>/board.h` with pin/clock definitions
2. Create `board/<BOARD_NAME>/board_init.c` with board-specific GPIO init
3. Create `board/<BOARD_NAME>/board_vectors.h` with interrupt assignments
4. Build with `make BOARD=<BOARD_NAME> APP=<app>`

## Adding a New Application

1. Create `app/<APP_NAME>/main.c`
2. Add additional `.c` files under `app/<APP_NAME>/` as needed
3. Include the HAL headers you need (`hal_clock.h`, `hal_gpio.h`, etc.)
4. Build with `make BOARD=983HH APP=<APP_NAME>`

The build system compiles every `*.c` file under `app/<APP_NAME>/`.

## 983 Manager

`983_manager` is the production-oriented 983HH app. It ports the shared
power-up sequence and all supported `bios-ver-11.txt` display profiles into C.

Current 983HH behavior:
- uses `board/983HH/board_init.c` to enable `1V8`, `1V15`, wait `20 ms`, then assert `PDB`
- uses bit-banged I2C on `P10_2/P10_3` for the serializer/deserializer init path
- supports all DIP/EDID profiles emitted into generated shared block/EDID tables
- fixes known bad EDID checksums during profile generation
- detects a serializer strapped at local address `0x10` and forces it back to `0x18` for Linux compatibility
- uses host-side verification to prove optimized profile blocks still match the
  original BIOS-derived flat init streams
- uses a committed golden reference to detect unintended built-in profile drift

See [docs/983_manager.md](docs/983_manager.md) for more detail.

## I2C Slave Interface

The `i2c_slave` app exposes a standard 16-bit sub-addressed I2C slave
(EEPROM-style, 24C256 compatible) with 64K register address space.

See [docs/i2c_register_map.md](docs/i2c_register_map.md) for the full
protocol specification and register layout.

## Memory Map (R7F701686)

| Region | Address | Size |
|--------|---------|------|
| Code Flash | `0x00000000` | 512 KB |
| EIINTTBL | `0x00000200` | 2 KB |
| .text, .const | `0x00000A00` | (follows) |
| Local RAM | `0xFEBF0000` | 32 KB |
| Stack top | (end of RAM) | 4 KB |

## License

MIT License - see [LICENSE](LICENSE).

The device header (`device/dr7f701686.dvf.h`) is provided by Renesas Electronics
Corporation and is subject to their license terms.
