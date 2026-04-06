;----------------------------------------------------------------------
; boot.asm - RH850/F1KM-S1 Boot Code
;
; Exception vector table, EI-level interrupt vector table (EIINTTBL),
; register initialization, and startup entry point.
;
; This file is part of rh850-baremetal-demo (MIT License).
;----------------------------------------------------------------------

    USE_TABLE_REFERENCE_METHOD .set 1

;----------------------------------------------------------------------
; Exception Vector Table
; Section "RESET" placed at 0x00000000 by linker.
; Each entry is 16-byte aligned.
;----------------------------------------------------------------------
    .section "RESET", text
    .align  512
    jr32    __start             ; 0x000: RESET

    .align  16
    syncp
    jr32    _Dummy              ; 0x010: SYSERR

    .align  16
    jr32    _Dummy              ; 0x020: (reserved)

    .align  16
    jr32    _Dummy              ; 0x030: FETRAP

    .align  16
    jr32    _Dummy_EI           ; 0x040: TRAP0

    .align  16
    jr32    _Dummy_EI           ; 0x050: TRAP1

    .align  16
    jr32    _Dummy              ; 0x060: RIE

    .align  16
    syncp
    jr32    _Dummy_EI           ; 0x070: FPP/FPI

    .align  16
    jr32    _Dummy              ; 0x080: UCPOP

    .align  16
    jr32    _Dummy              ; 0x090: MIP/MDP

    .align  16
    jr32    _Dummy              ; 0x0A0: PIE

    .align  16
    jr32    _Dummy              ; 0x0B0: (reserved)

    .align  16
    jr32    _Dummy              ; 0x0C0: MAE

    .align  16
    jr32    _Dummy              ; 0x0D0: (reserved)

    .align  16
    syncp
    jr32    _Dummy              ; 0x0E0: FENMI

    .align  16
    syncp
    jr32    _Dummy              ; 0x0F0: FEINT

    ; EI-level interrupt priority vectors (0x100 - 0x1F0)
    .align  16
    syncp
    jr32    _Dummy_EI           ; 0x100: INTn priority 0

    .align  16
    syncp
    jr32    _Dummy_EI           ; 0x110: INTn priority 1

    .align  16
    syncp
    jr32    _Dummy_EI           ; 0x120: INTn priority 2

    .align  16
    syncp
    jr32    _Dummy_EI           ; 0x130: INTn priority 3

    .align  16
    syncp
    jr32    _Dummy_EI           ; 0x140: INTn priority 4

    .align  16
    syncp
    jr32    _Dummy_EI           ; 0x150: INTn priority 5

    .align  16
    syncp
    jr32    _Dummy_EI           ; 0x160: INTn priority 6

    .align  16
    syncp
    jr32    _Dummy_EI           ; 0x170: INTn priority 7

    .align  16
    syncp
    jr32    _Dummy_EI           ; 0x180: INTn priority 8

    .align  16
    syncp
    jr32    _Dummy_EI           ; 0x190: INTn priority 9

    .align  16
    syncp
    jr32    _Dummy_EI           ; 0x1A0: INTn priority 10

    .align  16
    syncp
    jr32    _Dummy_EI           ; 0x1B0: INTn priority 11

    .align  16
    syncp
    jr32    _Dummy_EI           ; 0x1C0: INTn priority 12

    .align  16
    syncp
    jr32    _Dummy_EI           ; 0x1D0: INTn priority 13

    .align  16
    syncp
    jr32    _Dummy_EI           ; 0x1E0: INTn priority 14

    .align  16
    syncp
    jr32    _Dummy_EI           ; 0x1F0: INTn priority 15

;----------------------------------------------------------------------
; EI-level Interrupt Vector Table (EIINTTBL)
; 512 entries x 4 bytes = 2048 bytes.
; Section "EIINTTBL" placed at 0x00000200 by linker.
;
; Each entry is a 32-bit pointer to the ISR function.
; The table-reference bit (TB) in each channel's ICxxx register
; must be set by the HAL init code for the entry to be used.
;----------------------------------------------------------------------
    .section "EIINTTBL", const
    .align  512
    .public _INT_Vectors
_INT_Vectors:
    ; INT0 - INT75: no ISR assigned (76 entries)
    .rept   76
    .dw     #_Dummy_EI
    .endm
    ; INT76 = RIIC0 Transmit Data Empty (TI)
    .dw     #_hal_riic0_isr_ti
    ; INT77 = RIIC0 Error/Event (EE)
    .dw     #_hal_riic0_isr_ee
    ; INT78 = RIIC0 Receive Complete (RI)
    .dw     #_hal_riic0_isr_ri
    ; INT79 = RIIC0 Transmit End (TEI)
    .dw     #_hal_riic0_isr_tei
    ; INT80 - INT511: no ISR assigned (432 entries)
    .rept   432
    .dw     #_Dummy_EI
    .endm

;----------------------------------------------------------------------
; Default handlers (infinite loop)
;----------------------------------------------------------------------
    .section ".text", text
    .align  2
_Dummy:
    br      _Dummy

_Dummy_EI:
    br      _Dummy_EI

;----------------------------------------------------------------------
; Startup entry point
;----------------------------------------------------------------------
    .section ".text", text
    .align  2
    .public __start
__start:
    ; Zero all general-purpose registers (r1-r31)
    $nowarning
    mov     r0, r1
    $warning
    mov     r0, r2
    mov     r0, r3
    mov     r0, r4
    mov     r0, r5
    mov     r0, r6
    mov     r0, r7
    mov     r0, r8
    mov     r0, r9
    mov     r0, r10
    mov     r0, r11
    mov     r0, r12
    mov     r0, r13
    mov     r0, r14
    mov     r0, r15
    mov     r0, r16
    mov     r0, r17
    mov     r0, r18
    mov     r0, r19
    mov     r0, r20
    mov     r0, r21
    mov     r0, r22
    mov     r0, r23
    mov     r0, r24
    mov     r0, r25
    mov     r0, r26
    mov     r0, r27
    mov     r0, r28
    mov     r0, r29
    mov     r0, r30
    mov     r0, r31

    ; Clear system registers
    ldsr    r0, 0, 0            ; EIPC = 0
    ldsr    r0, 16, 0           ; CTPC = 0

    ; Hardware init (placeholder for RAM clear if needed)
    jarl    _hdwinit, lp

    ; Set up table-reference method for EI-level interrupts
$ifdef USE_TABLE_REFERENCE_METHOD
    mov     #_INT_Vectors, r6
    jarl    _set_table_reference_method, lp
$endif

    ; Continue to C runtime startup
    jr32    __cstart

;----------------------------------------------------------------------
; Hardware initialization
; Override GLOBAL_RAM_ADDR/END and LOCAL_RAM_ADDR/END to clear
; specific RAM ranges at boot if needed.
;----------------------------------------------------------------------
    GLOBAL_RAM_ADDR .set 0
    GLOBAL_RAM_END  .set 0
    LOCAL_RAM_ADDR  .set 0
    LOCAL_RAM_END   .set 0

    .align  2
_hdwinit:
    mov     lp, r14

    mov     GLOBAL_RAM_ADDR, r6
    mov     GLOBAL_RAM_END, r7
    jarl    _zeroclr4, lp

    mov     LOCAL_RAM_ADDR, r6
    mov     LOCAL_RAM_END, r7
    jarl    _zeroclr4, lp

    mov     r14, lp
    jmp     [lp]

;----------------------------------------------------------------------
; Clear memory (4-byte aligned, r6=start, r7=end)
;----------------------------------------------------------------------
    .align  2
_zeroclr4:
    br      .L.zeroclr4.check
.L.zeroclr4.loop:
    st.w    r0, [r6]
    add     4, r6
.L.zeroclr4.check:
    cmp     r6, r7
    bh      .L.zeroclr4.loop
    jmp     [lp]

$ifdef USE_TABLE_REFERENCE_METHOD
;----------------------------------------------------------------------
; Set table-reference method for EI-level interrupts
; r6 = base address of EIINTTBL
;----------------------------------------------------------------------
    .align  2
_set_table_reference_method:
    ; Set INTBP register (Interrupt Handler Table Base Pointer)
    ldsr    r6, 4, 1            ; INTBP <- r6
    jmp     [lp]
$endif

;----------------------------------------------------------------------
; end of boot.asm
;----------------------------------------------------------------------
