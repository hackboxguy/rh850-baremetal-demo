/*
 * lib_ringbuf.c - Lock-free SPSC ring buffer
 *
 * Uses head/tail indices with power-of-2 masking.
 * Producer only writes head, consumer only writes tail.
 * No memory barriers needed on single-core RH850 (no out-of-order execution).
 */

#include "lib_ringbuf.h"

void ringbuf_init(ringbuf_t *rb)
{
    rb->head = 0u;
    rb->tail = 0u;
}

uint8 ringbuf_put(ringbuf_t *rb, uint8 byte)
{
    uint32 next = (rb->head + 1u) & RINGBUF_MASK;

    if (next == rb->tail)
    {
        return 0u;             /* Full */
    }

    rb->buf[rb->head] = byte;
    rb->head = next;
    return 1u;
}

uint8 ringbuf_get(ringbuf_t *rb, uint8 *out)
{
    if (rb->head == rb->tail)
    {
        return 0u;             /* Empty */
    }

    *out = rb->buf[rb->tail];
    rb->tail = (rb->tail + 1u) & RINGBUF_MASK;
    return 1u;
}

uint32 ringbuf_count(const ringbuf_t *rb)
{
    return (rb->head - rb->tail) & RINGBUF_MASK;
}

uint8 ringbuf_is_empty(const ringbuf_t *rb)
{
    return (rb->head == rb->tail) ? 1u : 0u;
}
