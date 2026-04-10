# I2C Slave Rapid Transaction Issue (Investigation Notes)

## Status

**RESOLVED** — root cause identified and fixed.

## Root Cause

The EE ISR's NACK handler was clearing `NACKF` and resetting state to
`ST_IDLE` immediately, **without waiting for the STOP condition** that
the master sends right after NACK. This allowed the next transaction
to begin while the previous slave-transmit was still being retired
by the RIIC hardware, leaving the slave in a stale state.

## Fix

In slave-transmit mode (`ST_SENDING_DATA`/`ST_SEND_DONE`), after NACK
detection:
1. Read DRR to release SCL
2. **Poll-wait for STOP condition** (with timeout for safety)
3. Clear both NACKF and STOP flags atomically

This mirrors the Renesas Smart Configurator slave example pattern.
See `hal/hal_riic_slave.c` `hal_riic0_isr_ee()` NACK handler.

Additional changes that contributed to the fix:
- Use `data & 0x01` (received address byte's R/W bit) instead of
  `CR2.TRS` to determine direction in the address-match handler.
  This avoids potential confusion from stale TRS state.
- Disabled general call address (`SER = 0x01` instead of `0x09`) since
  it was unused by the protocol and widened the match conditions.

## Verification (after fix)

```bash
# Test 1: only 0x66 matches (no more phantom 0x67/0x68/0x6a)
i2cdetect -r -y 1
# Result: 60: ... 66 -- -- ... ✓

# Test 2: rapid 1-byte reads
for i in 1 2 3 4 5 6 7 8 9 10; do
  i2ctransfer -y 1 w2@0x66 0x00 0x00 r1@0x66
done
# Result: all 10 return 0x01 ✓

# Test 3: disptool i2cscan
./micropanel/bin/disptool --device=ioc --command=i2cscan
# Result: works, finds 0x30 and 0x6b ✓
```

---

## Original Investigation Notes (kept for history)

## Symptom

When the Pi4 master sends rapid back-to-back I2C transactions to the F1KM
slave (`0x66` for REMOTE_DISP), the slave returns wrong data on alternating
transactions.

### Reproduction

Direct Pi4-to-F1KM connection (FPDLink bypassed):

```bash
for i in 1 2 3 4 5 6 7 8 9 10; do
  r=$(i2ctransfer -y 1 w2@0x66 0x00 0x00 r1@0x66 2>&1)
  echo "wr $i: $r"
done
```

**Expected:** All 10 reads return `0x01` (FW_VERSION_MAJOR).

**Actual:**
```
wr 1: 0x01
wr 2: 0x42
wr 3: 0x01
wr 4: 0x42
wr 5: 0x01
wr 6: 0x42
...
```

Perfectly deterministic alternation: every other transaction returns
`0x42` (which is not stored in any register and is not a string char
from any literal in our code).

## What Works

- **Single transactions** — Always correct
- **Multi-byte burst reads** — Always correct (e.g., `r8@0x66` returns full
  device info bytes correctly)
- **With 500ms delay between transactions** — 100% success
- **The I2C bridge** (read 1-16 bytes from internal I2C devices) — Always works
- **Rapid write-only transactions** — All succeed

## What Fails

- **Rapid 1-byte reads with explicit address set** (`w2 ... r1`) — alternating wrong data
- **Pi4 disptool's poll loop** for `i2cscan` — fails to read device count after status=done

## Diagnostic Findings

### 1. Bus integrity is fine

When tested with `iocversion` running in a tight loop on terminal 1 while
`i2cscan` runs on terminal 2, terminal 1 shows **continuous successful reads**
(20+ in a row). The slave is responsive — no NACKs at the bus layer.

### 2. Multi-address phantom matching

`i2cdetect -r -y 1` shows the slave matching multiple addresses:

```
60: -- -- -- -- -- -- 66 67 68 -- 6a -- -- -- -- --
```

Only `0x66` should match. The phantom addresses (0x67, 0x68, 0x6a) suggest
the slave is in a stale state when `i2cdetect` probes the next address —
the previous probe's response is still being completed.

### 3. Not a clock-stretching issue

If it were clock stretching, we'd see I/O errors, not wrong data. Wrong
data means the slave is **responding correctly at the bus layer** but
serving from the wrong register.

### 4. The `0x42` value

`0x42` does not appear in:
- Any register definition
- Any string literal (`__DATE__`, `__TIME__`, `BOARD_NAME`, app name)
- Any constant in our code

It's computed at runtime and is the same on every alternating transaction.
The most likely explanation is that `g_reg_addr` is being corrupted to a
specific value during fast transactions.

## Hypotheses Tested (None Fixed It)

| Hypothesis | Test | Result |
|------------|------|--------|
| `SR2.TEND` not cleared in TEI ISR | Cleared TEND explicitly | No change |
| `FER.NACKE` causing slave-TX suspension | Disabled NACKE bit | No change |
| `SR1` lingering bits | Cleared full SR1 in STOP handler | No change |
| `SER.GCAE` (general call) matching extra addresses | Disabled GCAE | No change |
| Stale state at start of new transaction | Reset state on START in EE ISR | No change |
| Drain DRR in STOP handler unconditionally | Added unconditional drain | No change |
| FPDLink back-channel transport errors | Tested with direct Pi4-to-F1KM connection | Issue persists |
| Pi4 himax_tp driver interference | Unloaded driver | No change |

## Root Cause Suspicions

The most likely root causes (untested):

1. **CR2.TRS bit stuck after slave-transmit** — After a read transaction,
   the RIIC's TRS (transmit/receive select) bit may not auto-clear before
   the next transaction's address match. This would cause the slave to
   misinterpret the next write transaction's address bytes as data bytes
   to be transmitted.

2. **Race between RI ISR and EE ISR cleanup** — The `enable=false` pragma
   prevents nested interrupts, but the order of EE (STOP/NACK) and RI
   (next address match) might allow stale state.

3. **DRT pre-load behavior** — TI ISR pre-loads the next byte after master
   NACK. If the pre-load happens just before the next transaction starts,
   the DRT and internal slave state are out of sync.

## Workaround (Client Side)

Add a 10ms delay between rapid I2C transactions in client code:

```c
// In disptool poll loops
i2ctransfer_call(...);
usleep(10000);  // 10ms
i2ctransfer_call(...);
```

```python
# Python equivalent
import time
result = subprocess.check_output(["i2ctransfer", ...])
time.sleep(0.01)
result = subprocess.check_output(["i2ctransfer", ...])
```

## Workaround (Use Burst Reads)

For polling scenarios, use **multi-byte burst reads** instead of multiple
single-byte reads. Burst reads always work correctly:

```bash
# Instead of polling DBG_STATUS in a loop, sleep and read once:
sleep 1
i2ctransfer -y 1 w2@0x66 0x03 0x00 r4@0x66    # cmd, status, log, count
```

## Next Steps for Investigation

1. **Add diagnostic register** that returns current `g_reg_addr` value to
   confirm whether it's the address or the state machine that's corrupted

2. **Test with `__nop()` synchronization** between flag clears in ISRs

3. **Manually clear CR2.TRS** at the start of write transactions

4. **Compare with Renesas Smart Configurator** RIIC slave example for any
   missed register configurations

5. **Check Renesas RH850/F1KM hardware errata** for known RIIC slave issues

## Files

- `hal/hal_riic_slave.c` — RIIC0 slave driver (interrupt-driven)
- `app/display_manager/main.c` — I2C slave register handlers

## References

- Investigation chat: see git history around April 2026
- Related: I2C scan buffer + bridge implementation (commits e1bbb52..6374b04)
