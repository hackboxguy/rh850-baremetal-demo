# 983 Manager

## Purpose

`983_manager` is the production-oriented `983HH` application that ports the
display bring-up flow from `bios-scripts/bios-ver-11.txt` into bare-metal C.

The app is intended to replace BIOS-script initialization for the `983HH`
serializer board while preserving the same supported DIP-switch display
profiles and EDID payloads.

## High-Level Flow

1. Delay at startup to match the autorun/BOM power-up environment.
2. Run the shared `983HH` cold-boot sequence in `board/983HH/board_init.c`:
   - configure ports
   - enable `1V8` on `AP0_5`
   - enable `1V15` on `AP0_6`
   - wait `20 ms`
   - assert serializer `PDB` on `P9_6`
3. Switch to PLL clock.
4. Initialize bus 1 bit-bang helper (matching BIOS init coverage).
5. Probe the local serializer on bus 0.
6. If the serializer comes up strapped at `0x10`, force its local `DEVICE_ID`
   back to `0x18`.
7. Read DIP switches, select the generated profile, and execute the init table.

## Why Bit-Bang I2C Is Used

`983_manager` uses `hal_i2c_bitbang` on `P10_2/P10_3` for the serializer and
deserializer init path.

This was chosen because it proved robust on the populated `983HH` hardware
during bring-up, while the RIIC0 path was not needed for the final working
application. The common `hal_riic_master` infrastructure remains available for
other apps, but `983_manager` does not depend on it.

## Serializer Local Address Behavior

On the tested `983HH` hardware, the `DS90UH983` may power up strapped at local
address `0x10` instead of `0x18`.

`983_manager` handles this by:
- probing the local serializer address family
- writing `I2C_DEVICE_ID` register `0x00` to enable a local address override
- re-probing the serializer at `0x18`

The goal is Linux compatibility: after firmware init, Pi-side tools and drivers
can continue using the expected local serializer address `0x18`.

## Supported Profiles

Profiles are generated from:
- `bios-scripts/bios-ver-11.txt` for the stable base init flows
- `app/983_manager/panels.json` for the selectable profile manifest
- `app/983_manager/edid/*.bin` for the actual versioned EDID payloads

Generator / tooling:
- `tools/gen_983_manager_profiles.py`
- `tools/add_983_panel.py`

Generated output:
- `app/983_manager/profile_data.c`
- `app/983_manager/profile_data.h`

To regenerate after changing the BIOS reference or the profile manifest:

```bash
python3 rh850-baremetal-demo/tools/gen_983_manager_profiles.py
```

To add or replace a panel EDID while reusing an existing base profile:

```bash
python3 rh850-baremetal-demo/tools/add_983_panel.py \
    --key my_panel \
    --display-name "DIP4 13.3in 1920x720" \
    --base-profile dip4_10g8 \
    --edid /path/to/panel.bin \
    --core-mask 16 \
    --dip7-on 0 \
    --generate
```

Notes:
- `bios_source` in `panels.json` controls which non-EDID init sequence is reused.
- The EDID `.bin` files are now part of the `983_manager` source flow and should be versioned.
- If an EDID file is missing, the generator bootstraps it from the BIOS-derived default for that base profile.

Supported combinations currently include:
- `DIP0` 15.6" 2560x1440, 10.8 Gbps / 6.75 Gbps
- `DIP1` 14.6" 2560x1440
- `DIP2` 14.6" 1920x1080
- `DIP3` 12.3" 1920x720 OLDI
- `DIP4` 27" 4032x756, 10.8 Gbps / 6.75 Gbps
- `DIP5` 17.3" 2880x1620
- `DIP0 + DIP1` 35.6" 6480x960, 10.8 Gbps / 6.75 Gbps
- `DIP0 + DIP2` 12.3" 1920x720 DP, 10.8 Gbps / 6.75 Gbps

## EDID Notes

The generator fixes known bad EDID checksums found in the source BIOS script,
so the emitted C profile tables may intentionally differ from the raw script
where the script contained an invalid checksum byte.

## Debug Build

Build with UART debug:

```bash
make BOARD=983HH APP=983_manager DEBUG=on VERSION=01.01
```

Typical debug output shows:
- detected serializer local address
- whether the app forced `0x10 -> 0x18`
- selected DIP profile
- register reads during bring-up
- final `983_manager init complete`

## Flashing

```bash
./micropanel/bin/flashrh850.sh \
    --bios-autorun=output/983HH/983_manager/983HH_983_manager.bin \
    --npj=/home/pi/micropanel/share/sp6bins/config/983HH.npj
```

## Post-Init Linux Checks

Expected local bus view after a successful init:
- serializer at `0x18`
- deserializer alias at `0x2c`

Useful checks from Raspberry Pi:

```bash
i2cdetect -r -y 1
i2cget -y 1 0x18 0x05 b
```
