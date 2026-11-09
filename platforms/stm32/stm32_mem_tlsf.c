/*
 * STM32 内存后端：基于 vendor/tlsf 的静态堆分配器。
 *
 * 堆大小可通过编译选项 IOT_MEM_HEAP_SIZE 覆盖；
 * 首次分配时惰性创建 TLSF 控制结构与初始内存池。
 */
#include "hal_mem.h"

#include <stdint.h>
#include <string.h>

#include "tlsf.h"

#ifndef IOT_MEM_HEAP_SIZE
#define IOT_MEM_HEAP_SIZE (32U * 1024U)
#endif

/* 以 uint64_t 数组保证堆区按 8 字节对齐 */
static uint64_t s_heap[(IOT_MEM_HEAP_SIZE + 7U) / 8U];
static tlsf_t s_tlsf = NULL;

static void iot_mem_ensure(void)
{
    if (s_tlsf == NULL) {
        s_tlsf = tlsf_create_with_pool(s_heap, sizeof(s_heap));
    }
}

void *hal_malloc(size_t size)
{
    iot_mem_ensure();
    return (s_tlsf != NULL) ? tlsf_malloc(s_tlsf, size) : NULL;
}

void *hal_calloc(size_t count, size_t size)
{
    void *ptr;
    size_t total;

    if (count != 0U && size > ((size_t)-1) / count) {
        return NULL;
    }
    total = count * size;
    ptr = hal_malloc(total);
    if (ptr != NULL) {
        memset(ptr, 0, total);
    }
    return ptr;
}

void *hal_realloc(void *ptr, size_t size)
{
    iot_mem_ensure();
    return (s_tlsf != NULL) ? tlsf_realloc(s_tlsf, ptr, size) : NULL;
}

void hal_free(void *ptr)
{
    if (s_tlsf != NULL) {
        tlsf_free(s_tlsf, ptr);
    }
}
