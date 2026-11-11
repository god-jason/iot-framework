/*
 * STM32 内存后端：基于 vendor/tlsf 的静态堆分配器。
 *
 * 堆大小可通过编译选项 IOT_MEM_HEAP_SIZE 覆盖；
 * 首次分配时惰性创建 TLSF 控制结构与初始内存池。
 *
 * iot_mem_get_info() 的“已用”是各块 tlsf_block_size() 之和（不含块头），
 * 因此百分比略低于真实占用；用于状态显示足够。
 */
#include "iot_mem.h"

#include <stdint.h>
#include <string.h>

#include "tlsf.h"

#ifndef IOT_MEM_HEAP_SIZE
#define IOT_MEM_HEAP_SIZE (32U * 1024U)
#endif

/* 以 uint64_t 数组保证堆区按 8 字节对齐 */
static uint64_t s_heap[(IOT_MEM_HEAP_SIZE + 7U) / 8U];
static tlsf_t s_tlsf = NULL;
static size_t s_used;

static void iot_mem_ensure(void)
{
    if (s_tlsf == NULL) {
        s_tlsf = tlsf_create_with_pool(s_heap, sizeof(s_heap));
    }
}

void *iot_mem_malloc(size_t size)
{
    void *ptr;

    iot_mem_ensure();
    ptr = (s_tlsf != NULL) ? tlsf_malloc(s_tlsf, size) : NULL;
    if (ptr != NULL) {
        s_used += tlsf_block_size(ptr);
    }
    return ptr;
}

void *iot_mem_calloc(size_t count, size_t size)
{
    void *ptr;
    size_t total;

    if (count != 0U && size > ((size_t)-1) / count) {
        return NULL;
    }
    total = count * size;
    ptr = iot_mem_malloc(total);
    if (ptr != NULL) {
        memset(ptr, 0, total);
    }
    return ptr;
}

void *iot_mem_realloc(void *ptr, size_t size)
{
    void *out;
    size_t old_size = 0U;

    iot_mem_ensure();
    if (s_tlsf == NULL) {
        return NULL;
    }
    if (ptr != NULL) {
        old_size = tlsf_block_size(ptr);
    }
    out = tlsf_realloc(s_tlsf, ptr, size);
    if (out != NULL) {
        s_used = s_used - old_size + tlsf_block_size(out);
    }
    return out;
}

void iot_mem_free(void *ptr)
{
    if (s_tlsf != NULL) {
        if (ptr != NULL) {
            size_t size = tlsf_block_size(ptr);

            s_used = (s_used >= size) ? (s_used - size) : 0U;
        }
        tlsf_free(s_tlsf, ptr);
    }
}

int iot_mem_get_info(iot_mem_info_t *out)
{
    uint32_t total;

    if (out == NULL) {
        return -1;
    }

    total = (uint32_t)sizeof(s_heap);
    out->total_bytes = total;
    out->used_bytes = (uint32_t)((s_used < total) ? s_used : total);
    out->free_bytes = (total >= out->used_bytes) ? (total - out->used_bytes) : 0U;
    out->max_free_bytes = 0U;       /* TLSF 未暴露空闲链表统计 */
    return 0;
}
