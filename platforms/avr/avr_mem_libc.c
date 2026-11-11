/*
 * AVR 内存后端：使用 avr-libc 的 malloc/free。
 */
#include "iot_mem.h"

#include <stdlib.h>
#include <string.h>

void *iot_mem_malloc(size_t size)
{
    return malloc(size);
}

void *iot_mem_calloc(size_t count, size_t size)
{
    void *ptr;
    size_t total;

    if (count != 0U && size > ((size_t)-1) / count) {
        return NULL;
    }
    total = count * size;
    ptr = malloc(total);
    if (ptr != NULL) {
        memset(ptr, 0, total);
    }
    return ptr;
}

void *iot_mem_realloc(void *ptr, size_t size)
{
    return realloc(ptr, size);
}

void iot_mem_free(void *ptr)
{
    free(ptr);
}

int iot_mem_get_info(iot_mem_info_t *out)
{
    if (out == NULL) {
        return -1;
    }
    /* avr-libc 分配器不提供容量信息，状态显示按“未知”处理 */
    out->total_bytes = 0U;
    out->used_bytes = 0U;
    out->free_bytes = 0U;
    out->max_free_bytes = 0U;
    return 0;
}
