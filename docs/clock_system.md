# Clock System Reference

## F1KM-S1 Clock Architecture

F1KM-S1 has **PLL1 only** (no PLL0). The 983HH board has a 16 MHz MainOSC crystal.

## Default Clocks (After Reset, No PLL)

| Domain | Register | Value | Frequency |
|--------|----------|-------|-----------|
| CPUCLK | CKSC_CPUCLKS_CTL | 0x01 (HS IntOSC) | 8 MHz |
| CPUCLK2 | (derived) | CPUCLK / 2 | 4 MHz |
| IPERI1 | CKSC_IPERI1S_CTL | 0x01 = CPUCLK2 | 4 MHz |
| IPERI2 | CKSC_IPERI2S_CTL | 0x01 = CPUCLK2 | 4 MHz |
| LIN | CKSC_ILINS_CTL | 0x01 = CPUCLK2 | 4 MHz |

## After PLL Init

| Domain | Register | Value | Frequency |
|--------|----------|-------|-----------|
| CPUCLK | CKSC_CPUCLKS_CTL | 0x03 (PLL1) | 80 MHz |
| CPUCLK2 | (derived) | | 40 MHz |
| PPLLCLK | CKSC_PPLLCLKS_CTL | 0x03 (PLL1OUT) | 80 MHz |
| PPLLCLK2 | (derived) | | 40 MHz |
| IPERI1 | 0x02 = PPLLCLK | | 80 MHz |
| IPERI2 | 0x02 = PPLLCLK2 | | 40 MHz |
| LIN | 0x03 = PPLLCLK2 | | 40 MHz |
| CSI | 0x02 = PPLLCLK | | 80 MHz |

## PLL1 Configuration

```
PLL1C = 0x00010B3B
Input:  16 MHz (MainOSC)
VCO:    480 MHz
Output: PPLLCLK = 80 MHz, PPLLCLK2 = 40 MHz
```

## Protected Write Sequence

Clock control registers require protected writes:

```c
PROTCMD = 0xA5;
*reg =  value;
*reg = ~value;
*reg =  value;
```

| Domain | PROTCMD Register |
|--------|-----------------|
| AWO (MainOSC, IOHOLD, ADCA) | PROTCMD0 |
| ISO (CPU, PLL1, PERI, LIN, IIC, CAN, CSI) | PROTCMD1 |
| Port PODC | PPCMDn (per-port) |

## UART Baud Rate After Clock Switch

After `CKSC_CPUCLKS_CTL = 0x03`, the LIN clock changes from 4 MHz to 40 MHz
**immediately**. UART must be reinited right after the switch:

| PCLK | NSPB | BRP | Actual Baud | Error |
|------|------|-----|-------------|-------|
| 4 MHz | 4 (LWBR=0x30) | 8 | 111111 | 3.5% |
| 40 MHz | 16 (LWBR=0xF0) | 21 | 113636 | 1.4% |

## RIIC Clock Note

`CKSC_IIICS_CTL` on F1KM-S1 only accepts value `0x01`. The RIIC peripheral
requires PLL to be running; with PLL active and `IIICS=0x01`, the RIIC clock
is PPLLCLK2 (40 MHz). See `docs/riic_bringup.md` for details.
