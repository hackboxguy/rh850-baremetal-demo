;----------------------------------------------------------------------
; cstart.asm - RH850/F1KM-S1 C Runtime Startup
;
; Stack allocation, BSS/data section initialization via __INITSCT_RH
; (from libc.lib), FPU enable, and jump to main().
;
; This file is part of rh850-baremetal-demo (MIT License).
;----------------------------------------------------------------------

;----------------------------------------------------------------------
; System stack (4 KB)
; Placed in .stack.bss section -> RAM at link time.
; 4 KB is required when ISRs call UART debug functions.
;----------------------------------------------------------------------
STACKSIZE .set 0x1000

    .section ".stack.bss", bss
    .align  4
    .ds     (STACKSIZE)
    .align  4
_stacktop:

;----------------------------------------------------------------------
; Section initialization tables
; __INITSCT_RH uses these to:
;   - Copy .data ROM image to RAM (DSEC table)
;   - Zero .bss section (BSEC table)
;----------------------------------------------------------------------
    .section ".INIT_DSEC.const", const
    .align  4
    .dw     #__s.data, #__e.data, #__s.data.R

    .section ".INIT_BSEC.const", const
    .align  4
    .dw     #__s.bss, #__e.bss

;----------------------------------------------------------------------
; C runtime entry point (called from boot.asm __start)
;----------------------------------------------------------------------
    .section ".text", text
    .public __cstart
    .align  2
__cstart:
    ; Set stack pointer (top of stack, grows downward)
    mov     #_stacktop, sp

    ; Set global pointer (GP = r4, for SDA sections)
    mov     #__gp_data, gp

    ; Set element pointer (EP = r30, for short data access)
    mov     #__ep_data, ep

    ; Initialize .data and .bss sections
    ; __INITSCT_RH is provided by libc.lib (linked via Makefile)
    mov     #__s.INIT_DSEC.const, r6
    mov     #__e.INIT_DSEC.const, r7
    mov     #__s.INIT_BSEC.const, r8
    mov     #__e.INIT_BSEC.const, r9
    jarl32  __INITSCT_RH, lp

    ; Enable FPU if present
    stsr    6, r10, 1           ; r10 <- PID
    shl     21, r10
    shr     30, r10
    bz      .L.no_fpu          ; Skip if no FPU detected
    stsr    5, r10, 0           ; r10 <- PSW
    movhi   0x0001, r0, r11
    or      r11, r10
    ldsr    r10, 5, 0           ; PSW.CU0 = 1 (enable FPU)
    movhi   0x0002, r0, r11
    ldsr    r11, 6, 0           ; FPSR = 0x00020000
    ldsr    r0, 7, 0            ; FPEPC = 0
.L.no_fpu:

    ; Jump to main() via FERET for atomic PSW + PC transition
    stsr    5, r10, 0           ; r10 <- PSW
    ldsr    r10, 3, 0           ; FEPSW <- PSW (keep current mode)
    mov     #_exit, lp          ; Return address if main() returns
    mov     #_main, r10
    ldsr    r10, 2, 0           ; FEPC <- main
    feret

; If main() returns, halt here
_exit:
    br      _exit

;----------------------------------------------------------------------
; Dummy sections to ensure linker creates __s.xxx / __e.xxx symbols
;----------------------------------------------------------------------
    .section ".data", data
.L.dummy.data:
    .section ".bss", bss
.L.dummy.bss:
    .section ".const", const
.L.dummy.const:
    .section ".text", text
.L.dummy.text:

;----------------------------------------------------------------------
; end of cstart.asm
;----------------------------------------------------------------------
