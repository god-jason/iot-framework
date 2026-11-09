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
