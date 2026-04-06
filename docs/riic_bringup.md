# RIIC0 Bringup Reference

## Critical Discovery: Three Requirements

The RIIC hardware I2C required three non-obvious elements discovered via the
Smart Configurator generated code:

### 1. PBDC = 1 (Port Bidirectional Control)

Without PBDC, RIIC cannot read back SDA/SCL states. The state machine never
detects bus-free, and START conditions never complete.

### 2. PODC = 1 (Open Drain) + PDSC cleared

Required for I2C open-drain signaling. Both need protected write via PPCMD10:

```c
tmp = PORTPODC10;
PORTPPCMD10 = 0xA5u;
PORTPODC10 = (tmp | bit);
PORTPODC10 = ~(tmp | bit);
PORTPODC10 = (tmp | bit);
```

### 3. IER = 0xFC (Interrupt Enable Register)

SR2 status flags (TDRE, TEND, NACKF, etc.) are **only set when corresponding
IER bits are enabled**, even in polled mode. With IER=0x00, SR2 stays at 0x00.

## Pin Configuration Sequence

For each I2C pin (P10_2 SDA, P10_3 SCL):

1. Clear PIBC (input buffer)
2. Clear PBDC (bidirectional)
3. Set PM = input (initially)
4. Clear PMC (GPIO initially)
5. Clear PIPC
6. Clear PDSC (drive strength) - **protected write**
7. Set PODC (open-drain) - **protected write**
8. Set PBDC = 1 (**critical**)
9. Set PFC/PFCE/PFCAE for AF2
10. Set PMC = alt function
11. Set PM = output

## RIIC Init Sequence

```c
// Three-step reset
RIIC0.CR1.UINT32 &= 0x0Cu;     // Disable
RIIC0.CR1.UINT32 |= 0x40u;     // IICRST
RIIC0.CR1.UINT32 |= 0xC0u;     // ICE + IICRST

// Configure (while ICE=1, IICRST=1)
RIIC0.MR1.UINT32 = 0x30u;      // CKS=/8 -> 5 MHz ref
RIIC0.BRL.UINT32 = 0xF7u;      // 0xE0 | 0x17 (low period)
RIIC0.BRH.UINT32 = 0xF4u;      // 0xE0 | 0x14 (high period)
RIIC0.FER.UINT32 = 0x72u;      // SCLE + NFE + NACKE + MALE
RIIC0.IER.UINT32 = 0xFCu;      // Enable status flags

// Cancel internal reset
RIIC0.CR1.UINT32 &= 0x8Cu;     // Clear IICRST, keep ICE
```

## I2C Slave Specific Notes

### Interrupt Vector Table

CC-RH `#pragma interrupt(channel=N)` does NOT auto-patch the EIINTTBL when
building with a standalone Makefile. ISR addresses must be manually placed in
`boot.asm`:

- INT76: RIIC0 TI (transmit data empty)
- INT77: RIIC0 EE (error/event)
- INT78: RIIC0 RI (receive complete)
- INT79: RIIC0 TEI (transmit end)

### Stack Size

Default 512 bytes is too small when ISRs call UART functions. Causes silent
corruption of BSS globals. Use **4 KB minimum** (set in cstart.asm).

### AAS0 Flag

After slave address match, SR1.AAS0 remains set across multiple RI interrupts.
Must clear AAS0 after the first RI (address phase) to prevent re-triggering.

### STOP + RDRF Race

When master sends the last data byte, STOP and RDRF can be set simultaneously.
The EE ISR must not reset state if RDRF is pending - let the RI ISR process
the data first.

## Register Access

Use UINT32 access for RIIC registers with values in the low byte:

```c
RIIC0.CR1.UINT32 = 0x80u;           // Write
val = RIIC0.SR2.UINT32 & 0xFFu;     // Read
```

UINT8 access does NOT work correctly. UINT16[0] also works but UINT32 is
the proven pattern for both master and slave modes.
