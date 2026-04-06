/*
 * board_vectors.h - Interrupt vector assignments for 983HH
 *
 * INTC2 interrupt control register addresses for peripherals
 * used on this board.
 */

#ifndef BOARD_VECTORS_H
#define BOARD_VECTORS_H

/* RIIC0 interrupt channels */
#define IRQ_RIIC0_TI            76      /* Transmit data empty */
#define IRQ_RIIC0_EE            77      /* Error/event */
#define IRQ_RIIC0_RI            78      /* Receive complete */
#define IRQ_RIIC0_TEI           79      /* Transmit end */

/* INTC2 control registers for RIIC0 */
#define ICR_RIIC0_TI_ADDR       0xFFFFB098u
#define ICR_RIIC0_EE_ADDR       0xFFFFB09Au
#define ICR_RIIC0_RI_ADDR       0xFFFFB09Cu
#define ICR_RIIC0_TEI_ADDR      0xFFFFB09Eu

#endif /* BOARD_VECTORS_H */
