/*
 * lib_ringbuf.h - Lock-free single-producer single-consumer ring buffer
 *
 * Designed for ISR (producer) -> timer ISR (consumer) communication.
 * No locks, no disabling interrupts. Safe when:
 *   - Exactly one writer (e.g. I2C slave ISR)
 *   - Exactly one reader (e.g. OSTM timer ISR draining to UART)
 *
 * The buffer size must be a power of 2 for efficient masking.
 */

#ifndef LIB_RINGBUF_H
#define LIB_RINGBUF_H

#include "dr7f701686.dvf.h"

/* Buffer size (must be power of 2). 512 bytes handles ~5 ms of
 * debug output at 115200 baud without overflow. */
#define RINGBUF_SIZE        512u
#define RINGBUF_MASK        (RINGBUF_SIZE - 1u)

typedef struct
{
    volatile uint32 head;           /* Written by producer only */
    volatile uint32 tail;           /* Written by consumer only */
    volatile uint8  buf[RINGBUF_SIZE];
} ringbuf_t;

/* Initialize ring buffer (call before use) */
void ringbuf_init(ringbuf_t *rb);

/* Put one byte. Returns 1 on success, 0 if full. */
uint8 ringbuf_put(ringbuf_t *rb, uint8 byte);

/* Get one byte. Returns 1 on success (byte written to *out), 0 if empty. */
uint8 ringbuf_get(ringbuf_t *rb, uint8 *out);

/* Returns number of bytes available to read */
uint32 ringbuf_count(const ringbuf_t *rb);

/* Returns 1 if empty */
uint8 ringbuf_is_empty(const ringbuf_t *rb);

#endif /* LIB_RINGBUF_H */
