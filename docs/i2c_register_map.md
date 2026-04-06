# I2C Slave Register Map

Standard register map for RH850/F1KM-S1 I2C slave communication interface.

This specification defines the protocol and register layout used by all
applications that expose an I2C slave interface on the RH850 MCU.

## Bus Parameters

| Parameter | Value |
|-----------|-------|
| Slave address | 0x50 (7-bit) |
| Sub-addressing | 16-bit (EEPROM-style, 24C256/24C512 compatible) |
| SCL speed | ~100 kHz (standard mode) |
| Byte order | Big-endian (address high byte first) |

## Protocol

### Write (master -> slave)

```
[START] [0x50+W] [addr_hi] [addr_lo] [data0] [data1] ... [STOP]
```

The first two data bytes after the slave address set the 16-bit register
address. Subsequent bytes are written to consecutive registers starting
from that address. The address auto-increments after each byte and wraps
from 0xFFFF to 0x0000.

### Read (with address set)

```
[START] [0x50+W] [addr_hi] [addr_lo] [RESTART] [0x50+R] [data0] [data1] ... [NACK] [STOP]
```

A write phase sets the register address, then a repeated-start switches
to read mode. Data bytes are read from consecutive registers with
auto-increment.

### Current-address read (no address set)

```
[START] [0x50+R] [data0] [data1] ... [NACK] [STOP]
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

| Address | Name | Access | Description |
|---------|------|--------|-------------|
| `0x0000` | `FW_VERSION_MAJOR` | RO | Firmware major version (BCD) |
| `0x0001` | `FW_VERSION_MINOR` | RO | Firmware minor version (BCD) |
| `0x0002-0x00FF` | (reserved) | RO | Reserved for: HW revision, board ID, serial number, capabilities bitmap |

### Firmware Version Encoding

Version is stored as two BCD bytes. Examples:

| `0x0000` | `0x0001` | Interpreted as |
|----------|----------|----------------|
| `0x01` | `0x00` | v1.00 |
| `0x01` | `0x10` | v1.10 |
| `0x02` | `0x05` | v2.05 |
| `0x12` | `0x34` | v12.34 |

Set at build time: `make VERSION=01.10`

## Page 0x01: Status

| Address | Name | Access | Description |
|---------|------|--------|-------------|
| `0x0100` | `DIP_SWITCHES` | RO | DIP switch state (bit0=SW1 ... bit7=SW8) |
| `0x0101-0x01FF` | (reserved) | RO | Reserved for: ADC readings, bus status, uptime |

## Page 0x02: Control

| Address | Name | Access | Description |
|---------|------|--------|-------------|
| `0x0200` | `LED_CONTROL` | RW | LED state (bit0 = P9_6, 1=ON 0=OFF) |
| `0x0201-0x02FF` | (reserved) | RW | Reserved for: GPIO outputs, PWM duty, mode select |

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
