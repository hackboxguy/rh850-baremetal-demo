# MISRA C:2025 Deviation Log

Formal deviation records for the rh850-baremetal-demo project.

**Standard:** MISRA C:2025
**Tool:** cppcheck 2.13.0 + misra.py addon (C:2012 rule mapping)
**Project:** RH850/F1KM-S1 bare-metal firmware (983HH board)
**Total deviations:** 77 (across 8 rules)
**Last updated:** 2026-04-07

---

## DEV-001: Rule 1.2 — Language extensions (CC-RH compiler)

| Field | Value |
|-------|-------|
| **Rule** | 1.2 (Required) |
| **Description** | The code shall not use language extensions |
| **Scope** | Project-wide |
| **Occurrences** | Not detected by cppcheck (compiler-specific) |
| **Affected constructs** | `#pragma interrupt(enable=false, channel=N, fpu=true, callt=false)`, `__EI()`, `__nop()`, `$nowarning`/`$warning` (asm) |

**Justification:** These are mandatory CC-RH compiler intrinsics for RH850 interrupt handling. `#pragma interrupt` configures the ISR calling convention and interrupt channel binding. `__EI()` enables global interrupts (maps to the RH850 `ei` instruction). `__nop()` inserts a synchronization barrier. No portable C equivalent exists for these operations.

**Risk mitigation:** All compiler extensions are confined to HAL layer files (`hal_riic_slave.c`, `hal_riic_master.c`, `hal_timer.c`) and startup assembly. Application code does not use compiler extensions directly except `__EI()` in `main()`.

---

## DEV-002: Rule 2.5 — Unused macros in board headers

| Field | Value |
|-------|-------|
| **Rule** | 2.5 (Advisory) |
| **Description** | A macro should be referenced at least once |
| **Scope** | `board/983HH/board.h` |
| **Occurrences** | 22 |

**Justification:** Board headers define the complete hardware configuration (all pins, clocks, peripherals) as a single-file reference for porting. Each application uses a subset of these macros. Removing unused macros per-app would defeat the purpose of a board-swappable configuration layer.

**Risk mitigation:** None required — unused macros have no runtime effect.

---

## DEV-003: Rule 4.6 — Use of vendor type aliases instead of stdint.h

| Field | Value |
|-------|-------|
| **Rule** | 4.6 (Advisory) |
| **Description** | Types that indicate size and signedness should be used in place of the basic numerical types |
| **Scope** | Project-wide |
| **Occurrences** | Not detected by cppcheck |

**Justification:** The Renesas device header (`dr7f701686.dvf.h`) defines `uint32`, `uint16`, `uint8` as `unsigned long`, `unsigned short`, `unsigned char`. These are used throughout all register access patterns. Migrating to `stdint.h` types (`uint32_t` etc.) would require casts at every dvf.h boundary, adding noise without safety benefit — the types are identical in practice on RH850 (`unsigned long` = `uint32_t`).

**Risk mitigation:** Types are semantically equivalent. If `stdint.h` migration is required for a future compliance audit, it can be done mechanically (find-replace + boundary casts). Parked for future consideration.

---

## DEV-004: Rule 5.6 — Duplicate typedef names across translation units

| Field | Value |
|-------|-------|
| **Rule** | 5.6 (Required) |
| **Description** | A typedef name shall be a unique identifier |
| **Scope** | `hal/hal_riic_slave.h`, `lib/lib_ringbuf.h` |
| **Occurrences** | 6 |

**Justification:** Typedef names (`hal_riic_slave_write_cb`, `hal_riic_slave_read_cb`, `ringbuf_t`) are defined in headers with include guards. When cppcheck analyzes multiple translation units, it sees the same typedef from the shared header in each TU and reports duplicates. This is the standard C pattern for shared type definitions and is not a real violation.

**Risk mitigation:** Include guards prevent actual redefinition. Each typedef has exactly one definition site.

---

## DEV-005: Rule 5.7 — Duplicate tag names across translation units

| Field | Value |
|-------|-------|
| **Rule** | 5.7 (Required) |
| **Description** | A tag name shall be a unique identifier |
| **Scope** | `lib/lib_ringbuf.h` |
| **Occurrences** | 2 |

**Justification:** Same root cause as DEV-004. Struct tags in headers with include guards appear as duplicates when cppcheck scans multiple TUs. This is standard C header practice.

**Risk mitigation:** Include guards prevent redefinition.

---

## DEV-006: Rule 5.9 — Duplicate static function names across translation units

| Field | Value |
|-------|-------|
| **Rule** | 5.9 (Advisory) |
| **Description** | Identifiers with internal linkage should be unique across TUs |
| **Scope** | Multiple files |
| **Occurrences** | 5 |
| **Affected identifiers** | `delay()` in app main.c files, `riic0_pin_setup()` in hal_riic_master.c / hal_riic_slave.c |

**Justification:** Static functions with internal linkage are by definition confined to their translation unit. The C language explicitly permits identical names for static functions in different TUs. The `delay()` function is a trivial busy-wait used independently in each app. The `riic0_pin_setup()` functions perform the same pin configuration but are in separate drivers (master vs slave) that are never used simultaneously.

**Risk mitigation:** Functions have `static` linkage — no symbol collision possible at link time.

---

## DEV-007: Rule 8.5 — Function declarations in header files

| Field | Value |
|-------|-------|
| **Rule** | 8.5 (Required) |
| **Description** | An external object or function shall be declared once in one and only one file |
| **Scope** | `hal/hal_gpio.h` |
| **Occurrences** | 8 |

**Justification:** cppcheck reports function declarations in `hal_gpio.h` as violations because the header is included in multiple TUs. However, placing external function declarations in a header file is the standard C practice — the header IS the "one file" that provides the declaration, included where needed. This is a known cppcheck false positive for the multi-TU analysis mode.

**Risk mitigation:** Each function has exactly one declaration (in its header) and one definition (in its .c file).

---

## DEV-008: Rule 8.6 — Multiple definitions of `main()` across apps

| Field | Value |
|-------|-------|
| **Rule** | 8.6 (Required) |
| **Description** | An identifier with external linkage shall have exactly one external definition |
| **Scope** | `app/*/main.c` |
| **Occurrences** | 5 |

**Justification:** Each application has its own `main()` function. Only one app is compiled and linked per build (`make APP=<name>`). cppcheck scans all source files together and sees multiple `main()` definitions, but the build system ensures exactly one is linked.

**Risk mitigation:** Makefile `APP` variable selects exactly one `app/<name>/main.c` per build.

---

## DEV-009: Rule 11.4 — Pointer/integer casts for register access

| Field | Value |
|-------|-------|
| **Rule** | 11.4 (Required) |
| **Description** | A conversion should not be performed between a pointer to object and an integer type |
| **Scope** | `hal/hal_riic_slave.c`, `hal/hal_timer.c`, `hal/hal_clock.c` |
| **Occurrences** | 7 |
| **Affected patterns** | `(*(volatile uint16 *)0xFFFFB098u)`, `(*(volatile uint32 *)0xFFF8A800u)` |

**Justification:** Memory-mapped I/O register access is fundamental to bare-metal embedded programming. The RH850 peripheral registers exist at fixed hardware addresses that must be accessed via pointer casts. There is no portable C alternative. Every bare-metal MISRA project deviates on this rule.

**Risk mitigation:** All register addresses are derived from the Renesas hardware manual and verified on the 983HH board. Register access macros are defined in a small number of HAL files, not scattered throughout application code.

---

## DEV-010: Rule 15.5 — Multiple exit points (disapplied in MISRA C:2025)

| Field | Value |
|-------|-------|
| **Rule** | 15.5 (Advisory) |
| **Description** | A function should have a single point of exit at the end |
| **Scope** | Project-wide |
| **Occurrences** | 22 |

**Justification:** This rule is **disapplied in MISRA C:2025** (marked as no longer applicable). cppcheck's misra.py addon uses C:2012 rules where this was still active. Early returns are used in ISR handlers and polling functions for clarity and to reduce nesting depth.

**Risk mitigation:** No action required — rule is officially disapplied in the target standard.

---

## Summary

| Deviation | Rule | Count | Category | Nature |
|-----------|------|------:|----------|--------|
| DEV-001 | 1.2 | — | Required | Compiler extensions (not detected) |
| DEV-002 | 2.5 | 22 | Advisory | Board macro completeness |
| DEV-003 | 4.6 | — | Advisory | Vendor types (parked) |
| DEV-004 | 5.6 | 6 | Required | Header typedef (tool artifact) |
| DEV-005 | 5.7 | 2 | Required | Header tag (tool artifact) |
| DEV-006 | 5.9 | 5 | Advisory | Static function names |
| DEV-007 | 8.5 | 8 | Required | Header declarations (tool artifact) |
| DEV-008 | 8.6 | 5 | Required | Multi-app main() (build system) |
| DEV-009 | 11.4 | 7 | Required | Register pointer casts |
| DEV-010 | 15.5 | 22 | Advisory | Disapplied in C:2025 |
| **Total** | | **77** | | |
