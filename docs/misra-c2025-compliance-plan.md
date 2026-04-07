# MISRA C:2025 Compliance Plan

Plan for achieving MISRA C:2025 compliance on the rh850-baremetal-demo
codebase for automotive-grade robustness.

## 1. Target Standard

- **Standard:** MISRA C:2025 (Guidelines for the use of the C language
  in critical systems)
- **Compliance level:** Required + Mandatory rules enforced. Advisory rules
  adopted where practical, with documented rationale for any not adopted.
- **Reference PDF:** Purchase from https://misra.org.uk/product/misra-c2025/

## 2. Scope

### In scope (analyzed and compliant)

| Directory | Description |
|-----------|-------------|
| `hal/*.c` `hal/*.h` | Hardware Abstraction Layer |
| `lib/*.c` `lib/*.h` | Portable libraries |
| `app/*/main.c` | Application code |
| `board/*/*.c` `board/*/*.h` | Board configuration |

### Out of scope (excluded from analysis)

| Item | Reason |
|------|--------|
| `device/dr7f701686.dvf.h` | Renesas-owned vendor header, not modifiable |
| `startup/*.asm` | Assembly — outside MISRA C scope |
| CC-RH `libc.lib` | Compiler runtime library, not modifiable |

## 3. Tooling

### Phase 1: Open-source baseline

| Tool | Version | Purpose |
|------|---------|---------|
| cppcheck (open source) | latest | Static analysis engine |
| misra.py addon | bundled with cppcheck | MISRA C:2012 rule checking (~160 rules) |

The open-source misra.py addon covers MISRA C:2012 rules. Since C:2025
is 95%+ the same rules (renumbered/reorganized), this catches the vast
majority of real issues. The 4 new C:2025 rules (8.18, 8.19, 11.11, 19.3)
are manually auditable.

**Integration:** `make misra` target runs cppcheck on all in-scope sources,
excluding `device/` directory.

### Future: Certified tooling (when production requires it)

| Tool | Purpose |
|------|---------|
| cppcheck Premium | Full MISRA C:2025 coverage (since v25.8.0) |
| PC-lint Plus | Alternative, one-time license |

## 4. Phased Rollout

### Phase 1: Baseline (no code changes)

- [x] Create `misrac-2025` branch
- [x] Document compliance plan (this file)
- [x] Install cppcheck 2.13.0 on build system
- [x] Add `make misra` and `make misra-count` targets to Makefile
- [x] Run baseline scan: **209 violations across 21 rules in 17 files**
- [x] Categorize violations by severity and effort
- [x] Save baseline report to `docs/misra-baseline-report.md`

### Phase 2: Mechanical fixes — COMPLETE (132 violations fixed)

- [x] Rule 10.8: Moved casts to operands for composite expressions (37 fixes)
- [x] Rule 14.4: Explicit boolean comparisons in if/while (32 fixes)
- [x] Rule 15.6: Added `{ }` braces around single-statement bodies (21 fixes)
- [x] Rule 12.2: Cast shift operands to target width (12 fixes)
- [x] Rule 12.1: Added explicit parentheses (6 fixes)
- [x] Rule 13.5: Restructured while loops to remove side effects in && (5 fixes)
- [x] Rule 17.7: Cast unused return values to void (5 fixes)
- [x] Rule 17.8: Use local copy instead of modifying parameters (3 fixes)
- [x] Rules 15.4, 15.7, 10.7, 8.7, 8.4: One-off fixes (5 fixes)
- [x] Added standard boot banner (lib_boot.c/.h, conditional on DEBUG=on)
- [x] Added I2C bus recovery (9-clock SCL sequence) in hal_i2c_bitbang.c
- [x] All 5 apps build clean (release + debug, 10 builds total)
- [x] Remaining 77 violations are all documented deviations

### Phase 3: Deviations and type system — COMPLETE

- [x] Create `docs/misra_deviations.md` with 10 formal deviation records (DEV-001 to DEV-010)
- [x] Document permanent deviations:
  - DEV-001: Rule 1.2 — CC-RH language extensions (`#pragma interrupt`, `__EI()`, `__nop()`)
  - DEV-009: Rule 11.4 — Integer-to-pointer casts (register access, unavoidable in baremetal)
  - DEV-003: Rule 4.6 — Vendor types (`uint32` vs `uint32_t`) — deviated, parked for future
- [x] Document tool artifacts: DEV-004/005/007/008 (cppcheck multi-TU false positives)
- [x] Document disapplied rule: DEV-010 (Rule 15.5 disapplied in MISRA C:2025)

### Phase 4: Volatile access and control flow — COMPLETE

- [x] Restructure polling loops: `while (cond && --t)` → single-exit loop (done in Phase 2)
- [x] Review ISR handlers: all volatile accesses are simple reads/writes, no compound expressions
- [x] Rule 13.2/13.5: zero violations remaining after Phase 2 fixes

### Phase 5: Enforcement — COMPLETE

- [x] `make misra-count` returns 77 violations, all documented deviations (DEV-001 to DEV-010)
- [x] Deviation log created: `docs/misra_deviations.md`
- [x] Baseline report updated: `docs/misra-baseline-report.md`
- [ ] Hardware regression test on 983HH board (pending user verification)

## 5. Known Permanent Deviations

These are unavoidable in bare-metal RH850 development and will be
documented with full justification in `docs/misra_deviations.md`:

| Rule | Category | Description | Justification |
|------|----------|-------------|---------------|
| 1.2 | Required | Language extensions | CC-RH `#pragma interrupt`, `__EI()`, `__nop()` required for RH850 interrupt handling |
| 11.4 | Required | Pointer/integer cast | Memory-mapped register access (`*(volatile uint32 *)0xFFFFB098u`) is fundamental to baremetal |
| 21.1 | Required | Reserved identifiers | Renesas dvf.h uses reserved names — vendor code, not modifiable |
| 2.5 | Advisory | Unused macro | Board headers define macros used by subset of apps |

## 6. Hardware Validation

After each phase, the following apps must be tested on the 983HH board
to verify no functional regression:

| App | Test |
|-----|------|
| `blink_led` | LED blinks at ~1 Hz |
| `mirror_dip` | DIP switch 1 mirrors to LED |
| `i2c_slave` | FW version read, LED on/off, DIP read via i2ctransfer |
| `i2c_slave` (DEBUG=on) | Same + UART debug output via ring buffer |
| `i2c_master_pcf8574` | PCF8574A GPIOs toggle |
| `i2c_bitbang` | PCF8574A GPIOs toggle (bit-bang) |

### Test commands (Pi4)

```bash
# Firmware version
i2ctransfer -y 1 w2@0x50 0x00 0x00 r2@0x50

# LED ON / OFF
i2ctransfer -y 1 w3@0x50 0x02 0x00 0x01
i2ctransfer -y 1 w3@0x50 0x02 0x00 0x00

# DIP switches
i2ctransfer -y 1 w2@0x50 0x01 0x00 r1@0x50
```

## 7. Timeline

| Phase | Prerequisite | Estimated effort |
|-------|-------------|-----------------|
| Phase 1 | None | cppcheck install + Makefile target |
| Phase 2 | Phase 1 baseline | Mechanical fixes |
| Phase 3 | MISRA C:2025 PDF | Deviation documentation + type decisions |
| Phase 4 | Phase 3 | Volatile pattern rework |
| Phase 5 | Phase 4 + hardware | Final verification |

## 8. References

- MISRA C:2025 PDF: https://misra.org.uk/product/misra-c2025/
- cppcheck (open source): https://github.com/danmar/cppcheck
- cppcheck misra.py addon: https://github.com/danmar/cppcheck/blob/main/addons/misra.py
- cppcheck Premium (MISRA C:2025): https://www.cppcheck.com/
