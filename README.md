# RH850/F1KM-S1 Bare-Metal Demo

Modular bare-metal C examples for the Renesas RH850/F1KM-S1 MCU (R7F7016863),
built with the CC-RH compiler and GNU Make.

## Target Hardware

| Item | Value |
|------|-------|
| MCU | R7F7016863 (RH850/F1KM-S1, 100-pin LQFP) |
| Board | 983HH |
| Compiler | CC-RH V2.07.00 |
| Oscillator | 16 MHz MainOSC, 80 MHz CPU (with PLL) |
| Debug UART | RLIN32 on P0_13/P0_14, 115200 baud |
| I2C | RIIC0 on P10_2 (SDA) / P10_3 (SCL) |
| LED | P9_6 (active high) |
| DIP switches | AP0_7 through AP0_14 |

## Quick Start

```bash
# Build the LED blink example
make BOARD=983HH APP=blink_led

# Build I2C slave with debug output
make BOARD=983HH APP=i2c_slave DEBUG=on

# Flash the binary (on Raspberry Pi)
./micropanel/bin/flashrh850.sh \
    --bios-autorun=output/983HH/i2c_slave/983HH_i2c_slave.bin \
    --npj=/home/pi/micropanel/share/sp6bins/config/983HH.npj
```

## Available Applications

| APP | Description | Requires PLL |
|-----|-------------|:---:|
| `blink_led` | Blink LED on P9_6 | No |
| `mirror_dip` | Mirror DIP switch 1 to LED | No |
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
│   ├── board.h              Pin assignments, clock frequencies
│   ├── board_init.c         LED + DIP switch init
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
│   ├── blink_led/main.c
│   ├── mirror_dip/main.c
│   ├── i2c_bitbang/main.c
│   ├── i2c_master_pcf8574/main.c
│   └── i2c_slave/main.c
└── docs/                    Reference documentation
    └── i2c_register_map.md  I2C slave protocol and register spec
```

## Adding a New Board

1. Create `board/<BOARD_NAME>/board.h` with pin/clock definitions
2. Create `board/<BOARD_NAME>/board_init.c` with board-specific GPIO init
3. Create `board/<BOARD_NAME>/board_vectors.h` with interrupt assignments
4. Build with `make BOARD=<BOARD_NAME> APP=<app>`

## Adding a New Application

1. Create `app/<APP_NAME>/main.c`
2. Include the HAL headers you need (`hal_clock.h`, `hal_gpio.h`, etc.)
3. Build with `make BOARD=983HH APP=<APP_NAME>`

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
