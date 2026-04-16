# Pin Functions Reference (F1KM-S1, 100-pin)

## Alternate Function Encoding

| AF | PFCAE | PFCE | PFC |
|----|:-----:|:----:|:---:|
| AF1 | 0 | 0 | 0 |
| AF2 | 0 | 0 | 1 |
| AF3 | 0 | 1 | 0 |
| AF4 | 0 | 1 | 1 |
| AF5 | 1 | 0 | 0 |

## Pin Assignments (983HH Board)

| Pin | Function | AF | Notes |
|-----|----------|:--:|-------|
| P9_6 | Serializer PDB | GPIO | Active high enable for DS90UH983 power-down pin |
| AP0_7 | DIP switch 1 | GPIO | Needs APIBC enable |
| AP0_8-14 | DIP switch 2-8 | GPIO | Needs APIBC enable |
| P0_13 | RLIN32 RX | AF1 | Debug UART input |
| P0_14 | RLIN32 TX | AF1 | Debug UART output |
| P10_2 | RIIC0 SDA | AF2 | Open-drain + PBDC required |
| P10_3 | RIIC0 SCL | AF2 | Open-drain + PBDC required |
| P10_2 | I2C SDA (bit-bang) | GPIO | Alt: open-drain via PM toggle |
| P10_3 | I2C SCL (bit-bang) | GPIO | Alt: open-drain via PM toggle |

## Pin Mux Setup Order

1. Set PFC/PFCE/PFCAE **before** enabling PMC (avoids glitches)
2. Set PM (direction)
3. Set PODC if open-drain needed (protected write via PPCMDn)
4. Set PMC = 1 (switch from GPIO to alt function)
5. Set PIPC = 1 if peripheral needs bidirectional pin control
6. Set PIBC = 1 if input buffer needed for readback

## Atomic Set/Reset Registers

PSR, PMSR, and PMCSR use a 32-bit atomic pattern:
- Upper 16 bits = mask (which bits to modify)
- Lower 16 bits = value (new state)

```c
#define PSR_SET(bit)  ((uint32)0x00010001u << (bit))  // Set bit
#define PSR_CLR(bit)  ((uint32)0x00010000u << (bit))  // Clear bit
```

## Analog Port Input (AP0)

Analog ports need input buffer enabled for digital readback:

```c
PORTAPIBC0 |= (uint16)(1u << bit);
```

## Open-Drain (PODC) - Protected Write

Required for I2C pins:

```c
uint32 val = PORTPODC10 | (uint32)(1u << bit);
PORTPPCMD10 = 0xA5u;
PORTPODC10 =  val;
PORTPODC10 = ~val;
PORTPODC10 =  val;
```
