#include "iot_pool.h"

int iot_pool_init(iot_pool_t *pool, void *buffer,
                  uint32_t block_size, uint32_t block_count)
{
    uint32_t i;
    uint8_t *base;

    if (pool == NULL || buffer == NULL || block_size == 0 || block_count == 0) {
        return IOT_ERR_PARAM;
    }
    /* 块大小对齐到指针大小，保证内嵌 next 指针写入对齐 */
    if (block_size < (uint32_t)sizeof(void *)) {
        block_size = (uint32_t)sizeof(void *);
    }
    block_size = (block_size + (uint32_t)sizeof(void *) - 1u) &
                 ~((uint32_t)sizeof(void *) - 1u);

    base = (uint8_t *)buffer;
    pool->buffer = base;
    pool->block_size = block_size;
    pool->block_count = block_count;
    pool->free_count = block_count;
    pool->free_list = base;

    for (i = 0; i < block_count; i++) {
        void *next = (i + 1 < block_count)
                         ? (void *)(base + (i + 1) * block_size)
                         : NULL;
        void **p = (void **)(base + i * block_size);
        *p = next;
    }
    return IOT_OK;
}

void *iot_pool_alloc(iot_pool_t *pool)
{
    void *node;

    if (pool == NULL || pool->free_list == NULL || pool->free_count == 0) {
        return NULL;
    }
    node = pool->free_list;
    pool->free_list = *((void **)node);
    pool->free_count--;
    return node;
}

int iot_pool_free(iot_pool_t *pool, void *ptr)
{
    if (pool == NULL || ptr == NULL) {
        return IOT_ERR_PARAM;
    }
    *((void **)ptr) = pool->free_list;
    pool->free_list = ptr;
    pool->free_count++;
    return IOT_OK;
}

uint32_t iot_pool_available(iot_pool_t *pool)
{
    return (pool == NULL) ? 0 : pool->free_count;
}

uint32_t iot_pool_used(iot_pool_t *pool)
{
    if (pool == NULL) {
        return 0;
    }
    return pool->block_count - pool->free_count;
}