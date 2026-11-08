/*
 * 模拟器内存后端：直接封装标准库 malloc/free。
 */
#include "hal_mem.h"

#include <stdlib.h>

void *hal_malloc(size_t size)
{
    return malloc(size);
}

void *hal_calloc(size_t count, size_t size)
{
    return calloc(count, size);
}

void *hal_realloc(void *ptr, size_t size)
{
    return realloc(ptr, size);
}

void hal_free(void *ptr)
{
    free(ptr);
}
