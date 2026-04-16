# Setup Guide

This guide is for a developer starting from a fresh WSL2 Ubuntu machine and
wanting to build `rh850-baremetal-demo` quickly, with the same toolchain and
workflow used for the current `983_manager` baseline.

## Tested Host Environment

The current project flow is known to work with:
- WSL2
- Ubuntu 22.04 or 24.04
- Renesas CC-RH `V2.07.00`
- `srecord`
- `git`, `make`, `python3`

## Important WSL2 Notes

- Keep the repo under the Linux filesystem, for example `~/git-repos/...`
- Do not work from `/mnt/c/...`
- CC-RH is a Linux toolchain and path behavior under `/mnt/c` is unreliable
- The Makefile already builds in `/tmp/` as a workaround for CC-RH path issues

Recommended repo location:

```bash
mkdir -p ~/git-repos/codex
cd ~/git-repos/codex
```

## 10-Minute Quick Start

If you already have the CC-RH `.deb` installer downloaded:

```bash
sudo apt-get update
sudo apt-get install -y git make python3 unzip srecord minicom

# install CC-RH from the extracted Renesas package directory
sudo dpkg -i cc-rh-207_2.07.00_amd64.deb

# verify toolchain
/usr/local/Renesas/CC-RH/V2.07.00/bin/ccrh -version

# clone the repo into the Linux filesystem
cd ~/git-repos/codex
git clone <YOUR_REMOTE_URL> hh983v3-debug
cd hh983v3-debug/rh850-baremetal-demo

# first build + verification
make BOARD=983HH APP=983_manager DEBUG=off VERSION=01.02
make check-983-manager
make BOARD=983HH APP=983_manager size
```

Expected build outputs:
- `output/983HH/983_manager/release/983HH_983_manager.abs`
- `output/983HH/983_manager/release/983HH_983_manager.hex`
- `output/983HH/983_manager/release/983HH_983_manager.bin`
- `output/983HH/983_manager/release/983HH_983_manager.map`

## Step 1: Install Linux Dependencies

Install the basic host tools used by the build and verification flow:

```bash
sudo apt-get update
sudo apt-get install -y git make python3 unzip srecord minicom
```

What each package is used for:
- `git`: clone and update the repo
- `make`: top-level project build
- `python3`: profile generation and verification tools
- `unzip`: extract the Renesas download archive
- `srecord`: provides `srec_cat`, used to generate `.bin` from `.hex`
- `minicom`: simple UART monitor for debug builds

## Step 2: Download and Install CC-RH

### Renesas Portal

1. Create or use an existing Renesas account
2. Log in to the Renesas portal
3. Download:
   - `RH850 Compiler CC-RH V2.07.00 (Linux)`

### Install on WSL2

After downloading the Linux package zip, extract it and install the `.deb`.

Example using the file names currently known to work:

```bash
cd ~/Downloads
unzip RH850_Compiler_CC-RH_V2.07.00_Linux.zip -d cc-rh-v207
cd cc-rh-v207
sudo dpkg -i cc-rh-207_2.07.00_amd64.deb
```

If `dpkg` reports missing dependencies, fix them with:

```bash
sudo apt-get install -f
```

### Expected Install Path

The project Makefile assumes:

```text
/usr/local/Renesas/CC-RH/V2.07.00/
```

Verify it:

```bash
ls /usr/local/Renesas/CC-RH/V2.07.00/bin/ccrh
/usr/local/Renesas/CC-RH/V2.07.00/bin/ccrh -version
```

If your install path is different, update `CCRH_DIR` in [Makefile](Makefile).

## Step 3: Clone the Repo

Clone into the Linux home directory, not a Windows-mounted path:

```bash
mkdir -p ~/git-repos/codex
cd ~/git-repos/codex
git clone <YOUR_REMOTE_URL> hh983v3-debug
cd hh983v3-debug/rh850-baremetal-demo
```

## Step 4: First Build

Recommended first build:

```bash
make BOARD=983HH APP=983_manager DEBUG=off VERSION=01.02
```

Useful variants:

```bash
# build with UART debug enabled
make BOARD=983HH APP=983_manager DEBUG=on VERSION=01.02

# show linker size report
make BOARD=983HH APP=983_manager size

# print the resolved build configuration
make BOARD=983HH APP=983_manager info

# clean only this app/board output
make BOARD=983HH APP=983_manager clean
```

## Step 5: Verify Generated Assets

`983_manager` depends on generated profile data and committed EDID assets.
Always run the host-side verifier after generator or profile changes.

```bash
make check-983-manager
```

What this checks:
- `panels.json` selector consistency
- EDID asset existence and checksum validity
- optimized block/packed-write expansion vs BIOS-derived flat init streams
- generated `profile_data.c/.h` freshness
- committed `golden_reference.json` baseline consistency

If you intentionally changed the built-in baseline and want to refresh the
committed golden reference:

```bash
make refresh-983-manager-golden
make check-983-manager
```

## Step 6: Understand the Build Outputs

Build products are written to:

```text
output/<BOARD>/<APP>/<debug|release>/
```

For `983_manager` release builds:

```text
output/983HH/983_manager/release/
```

Files:
- `.abs`: linker output for debugger/programmer use
- `.hex`: Intel HEX image
- `.bin`: flat flashable image, generated via `srec_cat`
- `.map`: linker map and section sizes

## Step 7: Flashing

### Via `flashrh850.sh` on Raspberry Pi

```bash
./micropanel/bin/flashrh850.sh \
    --bios-autorun=output/983HH/983_manager/release/983HH_983_manager.bin \
    --npj=/home/pi/micropanel/share/sp6bins/config/983HH.npj
```

Notes:
- `--bios-autorun` replaces the BIOS image in flash
- for populated `983HH` serializer hardware, use `983_manager`
- do not use the old `blink_led` / `mirror_dip` apps as if `P9_6` were a harmless LED

## Step 8: Debug UART

Debug UART is:
- `P0_14` = TX from RH850
- `P0_13` = RX to RH850
- `115200 8N1`

Example monitor command:

```bash
minicom -D /dev/ttyS0 -b 115200
```

## Troubleshooting

### `ccrh: command not found`

Check:

```bash
ls /usr/local/Renesas/CC-RH/V2.07.00/bin/ccrh
```

If installed elsewhere, update `CCRH_DIR` in [Makefile](Makefile).

### `srec_cat: command not found`

Install `srecord`:

```bash
sudo apt-get install -y srecord
```

### Build fails from `/mnt/c/...`

Move the repo into the Linux filesystem, for example:

```bash
~/git-repos/codex/hh983v3-debug
```

### `.hex` exists but `.bin` does not

This usually means `srecord` is missing. Install it, then rebuild:

```bash
sudo apt-get install -y srecord
make BOARD=983HH APP=983_manager clean
make BOARD=983HH APP=983_manager
```

### `check-983-manager` fails

Usually one of:
- generated `profile_data.c/.h` are out of date
- EDID asset checksum drift
- `golden_reference.json` needs intentional refresh after reviewed baseline changes

Try:

```bash
python3 tools/gen_983_manager_profiles.py
make check-983-manager
```

Only if the built-in baseline intentionally changed:

```bash
make refresh-983-manager-golden
make check-983-manager
```
