/*
 * 模拟器内存后端：直接封装标准库 malloc/free。
 */
#include "iot_mem.h"

#include <stdlib.h>

void *iot_malloc(size_t size)
{
    return malloc(size);
}

void *iot_calloc(size_t count, size_t size)
{
    return calloc(count, size);
}

void *iot_realloc(void *ptr, size_t size)
{
    return realloc(ptr, size);
}

void iot_free(void *ptr)
{
    free(ptr);
}
