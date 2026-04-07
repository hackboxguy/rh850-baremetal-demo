/*
 * board_vectors.h - Interrupt vector assignments for REMOTE_DISP
 *
 * INTC2 interrupt control register addresses for peripherals
 * used on this board. Same MCU as 983HH (R7F7016863).
 */

#ifndef BOARD_VECTORS_H
#define BOARD_VECTORS_H

/* RIIC0 interrupt channels */
#define IRQ_RIIC0_TI            76
#define IRQ_RIIC0_EE            77
#define IRQ_RIIC0_RI            78
#define IRQ_RIIC0_TEI           79

/* INTC2 control registers for RIIC0 */
#define ICR_RIIC0_TI_ADDR       0xFFFFB098u
#define ICR_RIIC0_EE_ADDR       0xFFFFB09Au
#define ICR_RIIC0_RI_ADDR       0xFFFFB09Cu
#define ICR_RIIC0_TEI_ADDR      0xFFFFB09Eu

/* OSTM0 interrupt channel */
#define IRQ_OSTM0               84

/* INTC2 control register for OSTM0 */
#define ICR_OSTM0_ADDR          0xFFFFB0A8u

#endif /* BOARD_VECTORS_H */
