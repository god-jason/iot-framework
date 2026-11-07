#ifndef IOT_RINGBUF_H
#define IOT_RINGBUF_H

/*
 * 环形缓冲区（字节流，外部提供缓冲区）。
 * 用于 UART DMA / AT / 日志等场景。
 * 读写位置采用单调递增计数，容差任意 capacity（无需 2 的幂）。
 */

#include "iot_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t *buffer;
    uint32_t capacity;

    volatile uint32_t read_pos;
    volatile uint32_t write_pos;
} iot_ringbuf_t;

int iot_ringbuf_init(iot_ringbuf_t *rb, void *buffer, uint32_t size);

uint32_t iot_ringbuf_write(iot_ringbuf_t *rb, const void *data, uint32_t len);
uint32_t iot_ringbuf_read(iot_ringbuf_t *rb, void *data, uint32_t len);
uint32_t iot_ringbuf_peek(iot_ringbuf_t *rb, void *data, uint32_t len);
uint32_t iot_ringbuf_skip(iot_ringbuf_t *rb, uint32_t len);

uint32_t iot_ringbuf_available(const iot_ringbuf_t *rb);
uint32_t iot_ringbuf_free(const iot_ringbuf_t *rb);

void iot_ringbuf_reset(iot_ringbuf_t *rb);

#ifdef __cplusplus
}
#endif

#endif /* IOT_RINGBUF_H */