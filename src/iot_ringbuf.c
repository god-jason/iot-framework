#include "iot_ringbuf.h"

int iot_ringbuf_init(iot_ringbuf_t *rb, void *buffer, uint32_t size)
{
    if (rb == NULL || buffer == NULL || size == 0) {
        return -1;
    }
    rb->buffer = (uint8_t *)buffer;
    rb->capacity = size;
    rb->read_pos = 0;
    rb->write_pos = 0;
    return IOT_OK;
}

uint32_t iot_ringbuf_available(const iot_ringbuf_t *rb)
{
    if (rb == NULL) {
        return 0;
    }
    return rb->write_pos - rb->read_pos;
}

uint32_t iot_ringbuf_free(const iot_ringbuf_t *rb)
{
    if (rb == NULL) {
        return 0;
    }
    return rb->capacity - iot_ringbuf_available(rb);
}

uint32_t iot_ringbuf_write(iot_ringbuf_t *rb, const void *data, uint32_t len)
{
    const uint8_t *src = (const uint8_t *)data;
    uint32_t avail, free, n, i;

    if (rb == NULL || data == NULL || len == 0) {
        return 0;
    }
    free = iot_ringbuf_free(rb);
    n = (len < free) ? len : free;

    for (i = 0; i < n; i++) {
        uint32_t idx = (rb->write_pos + i) % rb->capacity;
        rb->buffer[idx] = src[i];
    }
    avail = iot_ringbuf_available(rb);
    (void)avail;
    rb->write_pos += n;
    return n;
}

uint32_t iot_ringbuf_read(iot_ringbuf_t *rb, void *data, uint32_t len)
{
    uint8_t *dst = (uint8_t *)data;
    uint32_t avail, n, i;

    if (rb == NULL || data == NULL || len == 0) {
        return 0;
    }
    avail = iot_ringbuf_available(rb);
    n = (len < avail) ? len : avail;

    for (i = 0; i < n; i++) {
        uint32_t idx = (rb->read_pos + i) % rb->capacity;
        dst[i] = rb->buffer[idx];
    }
    rb->read_pos += n;
    return n;
}

uint32_t iot_ringbuf_peek(iot_ringbuf_t *rb, void *data, uint32_t len)
{
    uint8_t *dst = (uint8_t *)data;
    uint32_t avail, n, i;

    if (rb == NULL || data == NULL || len == 0) {
        return 0;
    }
    avail = iot_ringbuf_available(rb);
    n = (len < avail) ? len : avail;

    for (i = 0; i < n; i++) {
        uint32_t idx = (rb->read_pos + i) % rb->capacity;
        dst[i] = rb->buffer[idx];
    }
    return n;
}

uint32_t iot_ringbuf_skip(iot_ringbuf_t *rb, uint32_t len)
{
    uint32_t avail, n;

    if (rb == NULL || len == 0) {
        return 0;
    }
    avail = iot_ringbuf_available(rb);
    n = (len < avail) ? len : avail;
    rb->read_pos += n;
    return n;
}

void iot_ringbuf_reset(iot_ringbuf_t *rb)
{
    if (rb == NULL) {
        return;
    }
    rb->read_pos = 0;
    rb->write_pos = 0;
}