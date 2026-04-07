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
- [ ] Install cppcheck on build system
- [ ] Add `make misra` target to Makefile
- [ ] Run baseline scan, record initial violation count
- [ ] Categorize violations by severity and effort
- [ ] Save baseline report to `docs/misra-baseline-report.md`

### Phase 2: Low-hanging fruit

- [ ] Add explicit casts for narrowing conversions
- [ ] Add missing `default:` cases in switch statements
- [ ] Fix implicit type promotions
- [ ] Add `u` suffixes to unsigned constants where missing
- [ ] Verify all include guards present and correct

### Phase 3: Deviations and type system

- [ ] Create `docs/misra_deviations.md` with deviation records
- [ ] Document permanent deviations:
  - Rule 1.2: CC-RH language extensions (`#pragma interrupt`, `__EI()`, `__nop()`)
  - Rule 11.4: Integer-to-pointer casts (register access, unavoidable in baremetal)
  - Rule 21.1/21.2: Reserved identifiers in `dr7f701686.dvf.h` (vendor code)
- [ ] Evaluate migration from Renesas `uint32`/`uint8` to `stdint.h`
  types (`uint32_t`/`uint8_t`) — Rule 4.6

### Phase 4: Volatile access and control flow

- [ ] Restructure polling loops to separate volatile reads from conditions
- [ ] Review ISR handlers for MISRA-compliant volatile access patterns
- [ ] Ensure no side effects in expressions with volatile operands (Rule 13.2)

### Phase 5: Enforcement

- [ ] `make misra` returns zero violations (excluding documented deviations)
- [ ] Add suppression comments with deviation references for permanent exceptions
- [ ] Baseline report updated to show final state
- [ ] Hardware regression test on 983HH board after all changes

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
