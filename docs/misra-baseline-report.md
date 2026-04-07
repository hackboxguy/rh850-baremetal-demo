# MISRA C Baseline Report

**Date:** 2026-04-07
**Tool:** cppcheck 2.13.0 + misra.py addon (MISRA C:2012 rules)
**Branch:** `misrac-2025`
**Board:** 983HH (R7F7016863, F1KM-S1)

## Summary

| Metric | Value |
|--------|-------|
| Total violations | **209** |
| Unique rules violated | 21 |
| Files with violations | 17 |
| Files excluded | `device/dr7f701686.dvf.h` (vendor), `startup/*.asm` |

## Violations by Rule

| Count | Rule | Category | Description | Fix approach |
|------:|------|----------|-------------|-------------|
| 39 | 10.8 | Required | Value of composite expression cast to wider type | Add intermediate casts |
| 32 | 14.4 | Required | Controlling expression of if/while shall be boolean | Use explicit `!= 0u` comparisons |
| 25 | 2.5 | Advisory | Unused macro | Board.h defines — suppress with deviation (macros used by subset of apps) |
| 22 | 15.5 | Advisory | Single point of exit from function | **Disapplied in MISRA C:2025** — suppress |
| 21 | 15.6 | Required | Loop body shall be compound statement | Add `{ }` around single-statement loops |
| 12 | 12.2 | Required | Shift count in range for type | Cast operand before shift |
| 8 | 8.5 | Required | External declaration in header | Move extern declarations to headers |
| 7 | 5.9 | Advisory | Internal linkage identifiers unique | Rename static identifiers |
| 7 | 11.4 | Required | Pointer/integer cast | **Permanent deviation** — baremetal register access |
| 6 | 5.6 | Required | Unique typedef names | Rename conflicting typedefs |
| 6 | 12.1 | Advisory | Operator precedence | Add explicit parentheses |
| 5 | 8.6 | Required | External identifier exactly one definition | Verify linkage |
| 5 | 17.7 | Required | Return value of non-void function used | Use `(void)` cast |
| 4 | 13.5 | Required | Persistent side effects in right-hand operand | Restructure expressions |
| 3 | 17.8 | Advisory | Parameter not modified, should be const | Add `const` qualifier |
| 2 | 5.7 | Required | Tag name unique | Rename conflicting tags |
| 1 | 8.4 | Required | Compatible declaration visible before definition | Add forward declaration |
| 1 | 8.7 | Advisory | Non-external object/function in one file should be static | Add `static` |
| 1 | 10.7 | Required | Composite expression implicit conversion | Add explicit cast |
| 1 | 15.4 | Required | At most one break/goto per loop | Restructure loop exit |
| 1 | 15.7 | Required | All if-else-if terminated with else | Add terminal `else` |

## Violations by File

| Count | File | Notes |
|------:|------|-------|
| 46 | `hal/hal_riic_master.c` | Register access casts, polling loops |
| 43 | `hal/hal_riic_slave.c` | ISR handlers, register access, state machine |
| 17 | `hal/hal_clock.c` | Protected write sequences, polling loops |
| 15 | `hal/hal_uart.c` | Baud rate calculation, UART register access |
| 12 | `hal/hal_i2c_bitbang.c` | GPIO bit manipulation |
| 12 | `board/983HH/board.h` | Unused macros (Rule 2.5) |
| 11 | `app/i2c_slave/main.c` | Callbacks, switch cases |
| 8 | `app/i2c_master_pcf8574/main.c` | UART calls, delay |
| 5 | `hal/hal_timer.c` | OSTM register access |
| 5 | `board/983HH/board_vectors.h` | Unused macros (Rule 2.5) |
| 5 | `app/i2c_bitbang/main.c` | Delay, main loop |
| 4 | `lib/lib_ringbuf.c` | Type widening |
| 4 | `hal/hal_gpio.h` | PSR macro casts |
| 3 | `hal/hal_gpio.c` | Shift operations |
| 3 | `app/blink_led/main.c` | Delay, main loop |
| 2 | `lib/lib_ringbuf.h` | Header declarations |
| 2 | `hal/hal_riic_slave.h` | Callback typedefs |
| 2 | `lib/lib_debug.h` | Macro definitions |
| 1 | `board/983HH/board_init.c` | Minor |
| 1 | `app/mirror_dip/main.c` | Minor |

## Categorization

### Auto-suppressible (no code change needed): ~52 violations

| Rules | Count | Reason |
|-------|------:|--------|
| 2.5 | 25 | Board macros used by app subsets — permanent deviation |
| 15.5 | 22 | Disapplied in MISRA C:2025 — suppress |
| 11.4 | 5 | Register address casts — permanent deviation |

### Mechanical fixes (Phase 2): ~120 violations

| Rules | Count | Fix |
|-------|------:|-----|
| 10.8 | 39 | Add intermediate casts for composite expressions |
| 14.4 | 32 | Add explicit `!= 0u` / `== 0u` to boolean contexts |
| 15.6 | 21 | Add `{ }` braces around single-statement loop bodies |
| 12.2 | 12 | Cast shift operand to target width before shifting |
| 12.1 | 6 | Add explicit parentheses for operator precedence |
| 5.9, 5.6, 5.7 | 15 | Rename conflicting identifiers |

### Requires analysis (Phase 3-4): ~37 violations

| Rules | Count | Notes |
|-------|------:|-------|
| 8.5, 8.6, 8.4, 8.7 | 15 | Linkage and declaration placement |
| 17.7 | 5 | Return value usage |
| 13.5 | 4 | Side effects in expressions |
| 17.8 | 3 | Const correctness |
| 10.7, 15.4, 15.7 | 3 | Expression/control flow one-offs |

## Projected Outcome

After all phases:
- **~52 documented deviations** (unavoidable in baremetal)
- **~0 remaining violations** (all mechanical fixes applied)
- **209 → 0** active violations (excluding deviations)
