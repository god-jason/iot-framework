#ifndef IOT_QUEUE_H
#define IOT_QUEUE_H

/*
 * 定长队列（数组实现，外部提供缓冲区，无动态内存）。
 */

#include "iot_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t *buffer;
    uint32_t item_size;
    uint32_t capacity;

    uint32_t head;
    uint32_t tail;
    uint32_t count;
} iot_queue_t;

int iot_queue_init(iot_queue_t *queue, void *buffer,
                   uint32_t item_size, uint32_t capacity);

int iot_queue_push(iot_queue_t *queue, const void *item);
int iot_queue_pop(iot_queue_t *queue, void *item);
int iot_queue_peek(iot_queue_t *queue, void *item);

bool iot_queue_empty(const iot_queue_t *queue);
bool iot_queue_full(const iot_queue_t *queue);

uint32_t iot_queue_size(const iot_queue_t *queue);
void iot_queue_clear(iot_queue_t *queue);

#ifdef __cplusplus
}
#endif

#endif /* IOT_QUEUE_H */