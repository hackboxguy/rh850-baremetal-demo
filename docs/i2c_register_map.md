# I2C Slave Register Map

Standard register map for RH850/F1KM-S1 I2C slave communication interface.

This specification defines the protocol and register layout used by all
applications that expose an I2C slave interface on the RH850 MCU.

## Bus Parameters

| Parameter | Value |
|-----------|-------|
| Slave address | Board-specific: 0x50 (983HH), 0x66 (REMOTE_DISP) |
| Sub-addressing | 16-bit (EEPROM-style, 24C256/24C512 compatible) |
| SCL speed | ~100 kHz (standard mode) |
| Byte order | Big-endian (address high byte first) |

## Protocol

### Write (master -> slave)

```
[START] [ADDR+W] [addr_hi] [addr_lo] [data0] [data1] ... [STOP]
```

The first two data bytes after the slave address set the 16-bit register
address. Subsequent bytes are written to consecutive registers starting
from that address. The address auto-increments after each byte and wraps
from 0xFFFF to 0x0000.

### Read (with address set)

```
[START] [ADDR+W] [addr_hi] [addr_lo] [RESTART] [ADDR+R] [data0] [data1] ... [NACK] [STOP]
```

A write phase sets the register address, then a repeated-start switches
to read mode. Data bytes are read from consecutive registers with
auto-increment.

### Current-address read (no address set)

```
[START] [ADDR+R] [data0] [data1] ... [NACK] [STOP]
```

Reads from the current address pointer (last set address, or where the
previous transaction left off). The address persists across STOP conditions,
matching standard EEPROM behavior.

## Register Map Overview

| Address Range | Page | Access | Description |
|---------------|------|--------|-------------|
| `0x0000-0x00FF` | Device Info | RO | Firmware version, hardware ID, capabilities |
| `0x0100-0x01FF` | Status | RO | Live sensor/input readings |
| `0x0200-0x02FF` | Control | RW | GPIO outputs, mode settings |
| `0x0300-0x03FF` | Configuration | RW | Persistent settings, thresholds |
| `0x0400-0x0FFF` | (reserved) | -- | Reserved for future standard pages |
| `0x1000-0x1FFF` | Diagnostics | RO | Temperature, voltage, error counters |
| `0x2000-0xEFFF` | (reserved) | -- | Reserved for application-specific use |
| `0xF000-0xFEFF` | Firmware Update | RW | Firmware image staging area |
| `0xFF00-0xFFFF` | Bootloader | RW | Update trigger, status, CRC |

Writes to read-only (RO) registers are silently ignored.
Reads from unimplemented registers return `0xFF`.

## Page 0x00: Device Info

| Address | Name | Access | Format | Description |
|---------|------|--------|--------|-------------|
| `0x0000` | `FW_VERSION_MAJOR` | RO | BCD | Firmware major version |
| `0x0001` | `FW_VERSION_MINOR` | RO | BCD | Firmware minor version |
| `0x0002` | `BUILD_YEAR_HI` | RO | BCD | Build year high (e.g. 0x20) |
| `0x0003` | `BUILD_YEAR_LO` | RO | BCD | Build year low (e.g. 0x26) |
| `0x0004` | `BUILD_MONTH` | RO | BCD | Build month (0x01-0x12) |
| `0x0005` | `BUILD_DAY` | RO | BCD | Build day (0x01-0x31) |
| `0x0006` | `BUILD_HOUR` | RO | BCD | Build hour (0x00-0x23) |
| `0x0007` | `BUILD_MINUTE` | RO | BCD | Build minute (0x00-0x59) |
| `0x0008-0x00FF` | (reserved) | RO | | Reserved for: HW revision, board ID, serial number |

### Firmware Version Encoding

Version is stored as two BCD bytes. Set at build time: `make VERSION=01.10`

| `0x0000` | `0x0001` | Interpreted as |
|----------|----------|----------------|
| `0x01` | `0x00` | v1.00 |
| `0x01` | `0x10` | v1.10 |
| `0x02` | `0x05` | v2.05 |

### Build Date/Time Encoding

All BCD format — each byte is human-readable in hex.

| Bytes (0x0002-0x0007) | Interpreted as |
|------------------------|----------------|
| `0x20 0x26 0x04 0x08 0x17 0x30` | 2026-04-08 17:30 |
| `0x20 0x25 0x11 0x15 0x09 0x45` | 2025-11-15 09:45 |

Read all device info in one shot (8 bytes):
```bash
# 983HH
i2ctransfer -y 1 w2@0x50 0x00 0x00 r8@0x50
# REMOTE_DISP
i2ctransfer -y 1 w2@0x66 0x00 0x00 r8@0x66
# Example: 0x01 0x10 0x20 0x26 0x04 0x08 0x17 0x30
#          v1.10     2026-04-08  17:30
```

Python parsing:
```python
data = [0x01, 0x10, 0x20, 0x26, 0x04, 0x08, 0x17, 0x30]
ver = f"v{data[0]:02X}.{data[1]:02X}"
date = f"{data[2]:02X}{data[3]:02X}-{data[4]:02X}-{data[5]:02X}"
time = f"{data[6]:02X}:{data[7]:02X}"
print(f"{ver} built {date} {time}")  # v01.10 built 2026-04-08 17:30
```

## Page 0x01: Status

| Address | Name | Access | Description |
|---------|------|--------|-------------|
| `0x0100` | `DIP_SWITCHES` | RO | DIP switch state (bit0=SW1 ... bit7=SW8) |
| `0x0101-0x01FF` | (reserved) | RO | Reserved for: ADC readings, bus status, uptime |

## Page 0x02: Control

| Address | Name | Access | Description |
|---------|------|--------|-------------|
| `0x0200` | `DISP_POWER_CMD` | RW | Display power command (0x00=OFF, 0x01=ON). REMOTE_DISP only. |
| `0x0200` | `LED_CONTROL` | RW | LED state (bit0 = P9_6, 1=ON 0=OFF). 983HH only. |
| `0x0201-0x02FF` | (reserved) | RW | Reserved for: backlight PWM, GPIO outputs, mode select |

### Display Power Control (REMOTE_DISP)

Two sources control display power with priority logic:
- **PCL** (AP0_4): hardware signal from vehicle (HIGH=OFF, always wins)
- **I2C** (reg 0x0200): software command from head-unit

```
Display ON  = (PCL=LOW) AND (I2C cmd=ON)
Display OFF = (PCL=HIGH) OR (I2C cmd=OFF)
```

Default I2C power state = ON at boot. Read current state at `0x0100`.

## Page 0x03: Debug Commands (REMOTE_DISP)

| Address | Name | Access | Description |
|---------|------|--------|-------------|
| `0x0300` | `DBG_CMD` | RW | Debug command (see table below) |
| `0x0301` | `DBG_STATUS` | RO | Command status: 0=idle, 1=running, 2=done |
| `0x0302` | `DBG_I2C_LOG` | RW | I2C slave debug prints: 0x00=off, 0x01=on (default) |
| `0x0303` | `SCAN_DEV_COUNT` | RO | Number of I2C1 devices found in last scan |
| `0x0304-0x030F` | (reserved) | | |
| `0x0310` | `BRIDGE_SLAVE` | RW | I2C bridge: target 7-bit slave address |
| `0x0311` | `BRIDGE_REG` | RW | I2C bridge: target register address |
| `0x0312` | `BRIDGE_LEN` | RW | I2C bridge: bytes to read/write (1-16) |
| `0x0313` | `BRIDGE_CMD` | RW | I2C bridge: 0x00=idle, 0x01=read, 0x02=write |
| `0x0314` | `BRIDGE_STATUS` | RO | I2C bridge: 0x00=idle, 0x01=run, 0x02=done, 0xFF=error |
| `0x0315-0x031F` | (reserved) | | |
| `0x0320-0x032F` | `BRIDGE_DATA` | RW | I2C bridge: 16-byte data buffer |
| `0x0330-0x037F` | (reserved) | | |
| `0x0380-0x03FF` | `SCAN_BUFFER` | RO | 128 bytes: scan result per 7-bit address |

### Debug Commands (write to 0x0300)

| Value | Name | Description |
|:-----:|------|-------------|
| `0x00` | Clear | No-op / clear command register |
| `0x01` | Scan + Print | Scan I2C1 bus, print i2cdetect table to UART, fill buffer |
| `0x02` | Scan to Buffer | Scan I2C1 bus, fill buffer only (works in release builds) |
| `0x03` | Flush | Clear scan buffer and reset status to idle |

### Scan Buffer (0x0380-0x03FF)

128 bytes, one per 7-bit I2C address (0x00-0x7F):
- `0x00` = no device (NACK, timeout, or reserved address)
- `0x01` = device found (ACK)

Addresses 0x00-0x02 and 0x78-0x7F are reserved (always 0x00).

### I2C1 Bus Scan — UART Mode (DEBUG builds)

Scans all 7-bit addresses (0x03-0x77) on the second I2C bus (P8_0/P8_1,
bit-banged) and prints an i2cdetect-style table to the UART debug terminal.
Also fills the scan buffer.

ISR debug prints are automatically suppressed during the scan for clean output.

```bash
# Trigger scan + UART print (DEBUG builds, address 0x66 for REMOTE_DISP)
i2ctransfer -y 1 w3@0x66 0x03 0x00 0x01
```

### I2C1 Bus Scan — Buffer Mode (all builds)

Scans I2C1 bus and stores results in the 128-byte scan buffer at 0x0380.
Works in both DEBUG and release builds (no UART needed).

```bash
# 1. Flush scan buffer
i2ctransfer -y 1 w3@0x66 0x03 0x00 0x03

# 2. Trigger scan to buffer
i2ctransfer -y 1 w3@0x66 0x03 0x00 0x02

# 3. Poll status until done (0x02)
i2ctransfer -y 1 w2@0x66 0x03 0x01 r1@0x66

# 4. Read device count
i2ctransfer -y 1 w2@0x66 0x03 0x03 r1@0x66

# 5. Read full scan buffer (128 bytes)
i2ctransfer -y 1 w2@0x66 0x03 0x80 r128@0x66
```

Python parsing example:
```python
import subprocess

# Read scan buffer (128 bytes from 0x0380)
raw = subprocess.check_output(
    ["i2ctransfer", "-y", "1", "w2@0x66", "0x03", "0x80", "r128@0x66"])
buf = [int(x, 16) for x in raw.split()]

print("I2C1 devices found:")
for addr in range(0x03, 0x78):
    if buf[addr] != 0:
        print(f"  0x{addr:02X} (write=0x{addr*2:02X}, read=0x{addr*2+1:02X})")
```

### I2C Slave Debug Control

Controls the R[]/W[] transaction debug prints on the UART terminal.
Useful for suppressing noise when reading the terminal for other output.

```bash
# Disable I2C slave transaction prints
i2ctransfer -y 1 w3@0x66 0x03 0x02 0x00

# Re-enable
i2ctransfer -y 1 w3@0x66 0x03 0x02 0x01
```

### I2C Bridge (relay to internal I2C1 bus)

Allows Pi4 to read/write any I2C device on the F1KM's internal I2C1 bus
(P8_0/P8_1) via the external I2C0 slave interface. Works in both release
and debug builds.

**Bridge registers (0x0310-0x032F):**

| Register | Name | Description |
|----------|------|-------------|
| `0x0310` | `BRIDGE_SLAVE` | Target 7-bit slave address |
| `0x0311` | `BRIDGE_REG` | Target register address |
| `0x0312` | `BRIDGE_LEN` | Number of bytes to read/write (1-16) |
| `0x0313` | `BRIDGE_CMD` | 0x00=idle, 0x01=read, 0x02=write |
| `0x0314` | `BRIDGE_STATUS` | 0x00=idle, 0x01=running, 0x02=done, 0xFF=error |
| `0x0320-0x032F` | `BRIDGE_DATA` | 16-byte data buffer |

**Read example — RTQ6749 PMIC fault register (slave 0x6B, reg 0x1D):**

```bash
# Single-shot setup (burst write: slave, reg, len, cmd)
i2ctransfer -y 1 w6@0x66 0x03 0x10 0x6B 0x1D 0x01 0x01

# Poll status until done (0x02) or error (0xFF)
i2ctransfer -y 1 w2@0x66 0x03 0x14 r1@0x66

# Read result (1 byte)
i2ctransfer -y 1 w2@0x66 0x03 0x20 r1@0x66
```

**Write example — write 0x55 to slave 0x6B register 0x10:**

```bash
# Set data first
i2ctransfer -y 1 w3@0x66 0x03 0x20 0x55

# Setup and trigger (slave, reg, len, cmd=write)
i2ctransfer -y 1 w6@0x66 0x03 0x10 0x6B 0x10 0x01 0x02

# Poll status
i2ctransfer -y 1 w2@0x66 0x03 0x14 r1@0x66
```

**Multi-byte read — read 4 bytes from slave 0x30 starting at reg 0x00:**

```bash
i2ctransfer -y 1 w6@0x66 0x03 0x10 0x30 0x00 0x04 0x01
# Poll status...
i2ctransfer -y 1 w2@0x66 0x03 0x20 r4@0x66
```

Python example:
```python
import subprocess, time

def bridge_read(slave, reg, length):
    """Read from internal I2C device via F1KM bridge."""
    # Setup + trigger (burst write to 0x0310)
    subprocess.run(["i2ctransfer", "-y", "1",
        f"w6@0x66", "0x03", "0x10",
        f"0x{slave:02X}", f"0x{reg:02X}", f"0x{length:02X}", "0x01"])
    # Poll status
    while True:
        r = subprocess.check_output(["i2ctransfer", "-y", "1",
            "w2@0x66", "0x03", "0x14", "r1@0x66"])
        status = int(r.strip().split()[0], 16)
        if status == 0x02: break  # done
        if status == 0xFF: return None  # error
        time.sleep(0.01)
    # Read data
    r = subprocess.check_output(["i2ctransfer", "-y", "1",
        "w2@0x66", "0x03", "0x20", f"r{length}@0x66"])
    return [int(x, 16) for x in r.strip().split()]

# Read RTQ6749 fault register
fault = bridge_read(0x6B, 0x1D, 1)
print(f"PMIC fault: 0x{fault[0]:02X}")

# Read RTQ6749 channel status
status = bridge_read(0x6B, 0x16, 1)
print(f"Channel status: 0x{status[0]:02X}")
```

## Page 0x10: Diagnostics

| Address | Name | Access | Format | Description |
|---------|------|--------|--------|-------------|
| `0x1000` | `TEMP_BL_RAW_HI` | RO | uint16 BE | Backlight NTC ADC raw value (bits 11:8) |
| `0x1001` | `TEMP_BL_RAW_LO` | RO | uint16 BE | Backlight NTC ADC raw value (bits 7:0) |
| `0x1002` | `TEMP_BL_DEGC_HI` | RO | int16 BE | Backlight temperature x10 (high byte) |
| `0x1003` | `TEMP_BL_DEGC_LO` | RO | int16 BE | Backlight temperature x10 (low byte) |
| `0x1004-0x1FFF` | (reserved) | RO | | Reserved for: supply voltages, FPGA temp, error counters |

### Temperature Format

Signed 16-bit integer, 0.1 degC resolution, big-endian:

| Bytes | Value | Temperature |
|-------|-------|-------------|
| `0x00 0xFD` | 253 | 25.3 C |
| `0x01 0x90` | 400 | 40.0 C |
| `0xFF 0x9C` | -100 | -10.0 C |

Pi4 Python example:
```python
import struct, subprocess
raw = subprocess.check_output(
    ["i2ctransfer", "-y", "1", "w2@0x50", "0x10", "0x02", "r2@0x50"])
val = struct.unpack(">h", bytes([int(x, 16) for x in raw.split()]))[0]
print(f"Temperature: {val / 10.0:.1f} C")
```

### NTC Sensor

- Physical pin: AP0_0 (ADCA0 channel ANI00)
- Sensor: 10K NTC, Beta=3950 (B25/85)
- Circuit: Vcc --- 10K pullup --- ADC --- NTC --- GND
- ADC: 12-bit, 3.3V reference
- Sampling: every 100ms via OSTM0 timer

## Pi4 Usage Examples

All examples use `i2ctransfer` (supports 16-bit sub-addressing):

```bash
# Read firmware version (2 bytes from 0x0000)
i2ctransfer -y 1 w2@0x50 0x00 0x00 r2@0x50
# Example output: 0x01 0x10  -> v1.10

# Read DIP switches (1 byte from 0x0100)
i2ctransfer -y 1 w2@0x50 0x01 0x00 r1@0x50
# Example output: 0xfe  -> SW1 off, SW2-8 on

# LED ON (write 0x01 to 0x0200)
i2ctransfer -y 1 w3@0x50 0x02 0x00 0x01

# LED OFF (write 0x00 to 0x0200)
i2ctransfer -y 1 w3@0x50 0x02 0x00 0x00

# Read LED state (1 byte from 0x0200)
i2ctransfer -y 1 w2@0x50 0x02 0x00 r1@0x50

# Bulk read: 16 bytes starting at 0x0000 (device info page)
i2ctransfer -y 1 w2@0x50 0x00 0x00 r16@0x50

# Current-address read (continues from last address)
i2ctransfer -y 1 r4@0x50

# Read backlight NTC raw ADC (2 bytes from 0x1000)
i2ctransfer -y 1 w2@0x50 0x10 0x00 r2@0x50
# Example output: 0x08 0x00  -> raw ADC = 2048

# Read backlight temperature (2 bytes from 0x1002, signed, /10 for degC)
i2ctransfer -y 1 w2@0x50 0x10 0x02 r2@0x50
# Example output: 0x00 0xFD  -> 253 = 25.3 C

# Read all diagnostics at once (4 bytes from 0x1000)
i2ctransfer -y 1 w2@0x50 0x10 0x00 r4@0x50
# Example output: 0x08 0x00 0x00 0xFD  -> raw=2048, temp=25.3C
```

**Note:** `i2cget`/`i2cset` only support 8-bit sub-addressing and cannot
be used with this interface. Always use `i2ctransfer`.

## Implementation Notes

### Adding a New Register

1. Choose an address within the appropriate page
2. Add the address constant to your app's `main.c`
3. Handle the address in the `on_read`/`on_write` callbacks
4. Update this document

### Adding a New Page

1. Choose an address range from the reserved space
2. Add the page to the table above
3. Document all registers in the page
4. Implement in the app callbacks

### Multi-byte Registers

For registers wider than 8 bits (e.g. 16-bit ADC value, 32-bit counter),
use consecutive addresses in big-endian order:

```
0x1000: TEMPERATURE_HI   (bits 15:8)
0x1001: TEMPERATURE_LO   (bits 7:0)
```

The master reads both bytes in a single transaction via auto-increment.

### Atomic Reads

Auto-increment reads are atomic from the master's perspective -- the slave
ISR serves bytes from a consistent snapshot per transaction. For registers
that change rapidly (e.g. counters), the app should latch the value on the
first byte read of a multi-byte register.
