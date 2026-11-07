#ifndef IOT_POOL_H
#define IOT_POOL_H

/*
 * 固定大小内存池。外部提供连续内存，空闲块通过内嵌指针串成链表。
 * 要求 block_size >= sizeof(void*)，且 buffer 按指针对齐。
 */

#include "iot_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t *buffer;
    uint32_t block_size;
    uint32_t block_count;
    uint32_t free_count;

    void *free_list;
} iot_pool_t;

int iot_pool_init(iot_pool_t *pool, void *buffer,
                  uint32_t block_size, uint32_t block_count);

void *iot_pool_alloc(iot_pool_t *pool);
int iot_pool_free(iot_pool_t *pool, void *ptr);

uint32_t iot_pool_available(iot_pool_t *pool);
uint32_t iot_pool_used(iot_pool_t *pool);

#ifdef __cplusplus
}
#endif

#endif /* IOT_POOL_H */