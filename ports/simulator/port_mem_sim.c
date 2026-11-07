/*
 * 模拟器内存后端：直接封装标准库 malloc/free。
 */
#include "port_mem.h"

#include <stdlib.h>

void *port_malloc(size_t size)
{
    return malloc(size);
}

void *port_calloc(size_t count, size_t size)
{
    return calloc(count, size);
}

void *port_realloc(void *ptr, size_t size)
{
    return realloc(ptr, size);
}

void port_free(void *ptr)
{
    free(ptr);
}
