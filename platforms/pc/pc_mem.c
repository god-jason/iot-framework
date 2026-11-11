/*
 * 模拟器内存后端：直接封装标准库 malloc/free。
 */
#include "iot_mem.h"

#include <stdlib.h>

void *iot_mem_malloc(size_t size)
{
    return malloc(size);
}

void *iot_mem_calloc(size_t count, size_t size)
{
    return calloc(count, size);
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
    /* 标准库分配器不提供容量信息，状态显示按“未知”处理 */
    out->total_bytes = 0U;
    out->used_bytes = 0U;
    out->free_bytes = 0U;
    out->max_free_bytes = 0U;
    return 0;
}
