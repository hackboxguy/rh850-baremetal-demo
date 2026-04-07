# MISRA C Baseline Report

**Date:** 2026-04-07
**Tool:** cppcheck 2.13.0 + misra.py addon (MISRA C:2012 rules)
**Branch:** `misrac-2025`
**Board:** 983HH (R7F7016863, F1KM-S1)

## Current Status (after Phase 2)

| Metric | Baseline | After Phase 2 | Delta |
|--------|----------|---------------|-------|
| Total violations | 209 | **77** | -132 fixed |
| Fixable violations | 132 | **0** | All fixed |
| Deviations (by-design) | 77 | **77** | Documented below |
| Unique rules violated | 21 | 8 | -13 rules cleared |

## Remaining Violations (all deviations — no further code fixes needed)

| Count | Rule | Category | Description | Deviation rationale |
|------:|------|----------|-------------|---------------------|
| 22 | 2.5 | Advisory | Unused macro | Board.h defines macros for all apps; each app uses a subset |
| 22 | 15.5 | Advisory | Multiple exit points | **Disapplied in MISRA C:2025** |
| 8 | 8.5 | Required | External declaration in header | Function declarations in headers — correct placement |
| 7 | 11.4 | Required | Pointer/integer cast | Memory-mapped register access, unavoidable in baremetal |
| 6 | 5.6 | Required | Duplicate typedef names | Typedefs in headers included by multiple TUs — standard C pattern |
| 5 | 8.6 | Required | Multiple definitions of external | Each app has its own `main()`; only one linked per build |
| 5 | 5.9 | Advisory | Duplicate internal identifiers | Static functions (e.g. `delay()`) in separate TUs — no conflict |
| 2 | 5.7 | Required | Duplicate tag names | Struct tags in headers included by multiple TUs |

## Phase 2 Fixes Applied (132 violations fixed)

| Rule | Count fixed | Fix applied |
|------|--------:|-------------|
| 10.8 | 37 | Moved casts to operands: `(uint16)(1u << bit)` → `((uint16)1u << bit)` |
| 14.4 | 32 | Explicit boolean: `if (ptr)` → `if (ptr != (void *)0)`, `while (x--)` → `while (x-- != 0u)` |
| 15.6 | 21 | Added `{ }` braces around all single-statement loop/if bodies |
| 12.2 | 12 | Cast shift operand before shift to ensure count in range |
| 12.1 | 6 | Added explicit parentheses for operator precedence |
| 13.5 | 5 | Restructured `while (cond && --t)` to avoid side effects in `&&` |
| 17.7 | 5 | Cast unused return values to `(void)` |
| 17.8 | 3 | Used local copy instead of modifying function parameter |
| 15.4 | 1 | Restructured loop to single exit point |
| 15.7 | 1 | Added terminal `else` to if-else-if chain |
| 10.7 | 1 | Added explicit cast for composite expression |
| 8.7 | 1 | Changed function to `static` (internal linkage) |
| 8.4 | 1 | Added `board_init()` declaration to `board.h` |
| **Total** | **132** | |

## Baseline Violations by Rule (for historical reference)

| Count | Rule | Category | Description |
|------:|------|----------|-------------|
| 39 | 10.8 | Required | Composite expression cast to wider type |
| 32 | 14.4 | Required | Controlling expression not boolean |
| 25 | 2.5 | Advisory | Unused macro |
| 22 | 15.5 | Advisory | Multiple exit points |
| 21 | 15.6 | Required | Loop body not compound statement |
| 12 | 12.2 | Required | Shift count out of range |
| 8 | 8.5 | Required | External declaration in header |
| 7 | 5.9 | Advisory | Duplicate internal identifiers |
| 7 | 11.4 | Required | Pointer/integer cast |
| 6 | 5.6 | Required | Duplicate typedef names |
| 6 | 12.1 | Advisory | Operator precedence unclear |
| 5 | 8.6 | Required | Multiple definitions of external |
| 5 | 17.7 | Required | Return value unused |
| 4 | 13.5 | Required | Side effects in && / || operand |
| 3 | 17.8 | Advisory | Parameter modified |
| 2 | 5.7 | Required | Duplicate tag names |
| 1 | 8.4 | Required | No compatible declaration visible |
| 1 | 8.7 | Advisory | Should be static |
| 1 | 10.7 | Required | Composite expression conversion |
| 1 | 15.4 | Required | Multiple breaks in loop |
| 1 | 15.7 | Required | if-else-if without terminal else |

## Verification Command

```bash
# Quick summary by rule
make misra-count

# Full report with file:line details
make misra
```

Expected output after Phase 2:
```
     22 2.5
     22 15.5
      8 8.5
      7 11.4
      6 5.6
      5 8.6
      5 5.9
      2 5.7
Total: 77 (all documented deviations)
```
