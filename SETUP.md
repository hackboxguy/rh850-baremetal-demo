# Setup Guide

## Prerequisites

### CC-RH Compiler

Install the Renesas CC-RH compiler (V2.07.00 or later) to:
```
/usr/local/Renesas/CC-RH/V2.07.00/
```

The Makefile expects this path. Adjust `CCRH_DIR` in the Makefile if your
installation differs.

### srec_cat (SRecord)

Used to convert Intel HEX to flat binary for flashing:
```bash
sudo apt install srecord
```

### WSL Note

CC-RH is a 32-bit Linux binary. On WSL2, it cannot access `/mnt/c/` paths.
The Makefile works around this by compiling in `/tmp/`. Source files are
automatically copied there during the build.

## Building

```bash
# Simple LED blink (no PLL, no UART)
make BOARD=983HH APP=blink_led

# I2C slave with debug prints
make BOARD=983HH APP=i2c_slave DEBUG=on

# Show build configuration
make BOARD=983HH APP=i2c_slave info

# Clean build artifacts
make BOARD=983HH APP=i2c_slave clean
```

Build outputs go to `output/<BOARD>/<APP>/`:
- `<BOARD>_<APP>.abs` - ELF executable (for debugger)
- `<BOARD>_<APP>.hex` - Intel HEX
- `<BOARD>_<APP>.bin` - Flat binary (for flash programmer)
- `<BOARD>_<APP>.map` - Linker map file

## Flashing

### Via flashrh850.sh (Raspberry Pi)

```bash
./micropanel/bin/flashrh850.sh \
    --bios-autorun=output/983HH/blink_led/983HH_blink_led.bin \
    --npj=/home/pi/micropanel/share/sp6bins/config/983HH.npj
```

The flash script handles:
1. GPIO 27 toggle -> FLMD0 (flash mode entry)
2. GPIO 18 toggle -> RESET
3. `rh850flash` programming via `/dev/ttyS0`
4. Normal mode restore and reset

**Warning:** `--bios-autorun` replaces the BIOS in flash. Re-flash the
original BIOS to restore normal scripting/autorun functionality.

### Flash Configuration

- Config file: `config/983HH.npj`
- Device index: 63 (R7F7010553)
- Serial port: `/dev/ttyS0`

## Device Header

The repository includes `device/dr7f701686.dvf.h` from Renesas. If you need
to regenerate it:

1. Install Renesas e2 studio with Smart Configurator
2. Create a project targeting R7F701686
3. Export the device header
4. Copy to `device/dr7f701686.dvf.h`

## Debug UART

Connect to the debug UART at 115200 baud, 8N1:
- P0_14 = TX (from RH850)
- P0_13 = RX (to RH850)

On Raspberry Pi: `/dev/ttyS0`

```bash
# Monitor UART output
minicom -D /dev/ttyS0 -b 115200
```
