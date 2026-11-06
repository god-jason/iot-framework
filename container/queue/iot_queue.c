#include "iot_queue.h"
#include "iot_memory.h"

int iot_queue_init(iot_queue_t *queue, void *buffer,
                   uint32_t item_size, uint32_t capacity)
{
    if (queue == NULL || buffer == NULL || item_size == 0 || capacity == 0) {
        return IOT_ERR_PARAM;
    }
    queue->buffer = (uint8_t *)buffer;
    queue->item_size = item_size;
    queue->capacity = capacity;
    queue->head = 0;
    queue->tail = 0;
    queue->count = 0;
    return IOT_OK;
}

int iot_queue_push(iot_queue_t *queue, const void *item)
{
    uint8_t *dst;

    if (queue == NULL || item == NULL) {
        return IOT_ERR_PARAM;
    }
    if (queue->count >= queue->capacity) {
        return IOT_ERR_OVERFLOW;
    }
    dst = queue->buffer + ((iot_size_t)queue->tail * queue->item_size);
    iot_memory_copy(dst, item, queue->item_size);
    queue->tail = (queue->tail + 1) % queue->capacity;
    queue->count++;
    return IOT_OK;
}

int iot_queue_pop(iot_queue_t *queue, void *item)
{
    const uint8_t *src;

    if (queue == NULL || item == NULL) {
        return IOT_ERR_PARAM;
    }
    if (queue->count == 0) {
        return IOT_ERR_AGAIN;
    }
    src = queue->buffer + ((iot_size_t)queue->head * queue->item_size);
    iot_memory_copy(item, src, queue->item_size);
    queue->head = (queue->head + 1) % queue->capacity;
    queue->count--;
    return IOT_OK;
}

int iot_queue_peek(iot_queue_t *queue, void *item)
{
    const uint8_t *src;

    if (queue == NULL || item == NULL) {
        return IOT_ERR_PARAM;
    }
    if (queue->count == 0) {
        return IOT_ERR_AGAIN;
    }
    src = queue->buffer + ((iot_size_t)queue->head * queue->item_size);
    iot_memory_copy(item, src, queue->item_size);
    return IOT_OK;
}

bool iot_queue_empty(const iot_queue_t *queue)
{
    return (queue == NULL) || (queue->count == 0);
}

bool iot_queue_full(const iot_queue_t *queue)
{
    return (queue != NULL) && (queue->count >= queue->capacity);
}

uint32_t iot_queue_size(const iot_queue_t *queue)
{
    return (queue == NULL) ? 0 : queue->count;
}

void iot_queue_clear(iot_queue_t *queue)
{
    if (queue == NULL) {
        return;
    }
    queue->head = 0;
    queue->tail = 0;
    queue->count = 0;
}