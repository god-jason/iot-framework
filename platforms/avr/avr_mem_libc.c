/*
 * AVR 内存后端：使用 avr-libc 的 malloc/free。
 */
#include "hal_mem.h"

#include <stdlib.h>
#include <string.h>

void *hal_malloc(size_t size)
{
    return malloc(size);
}

void *hal_calloc(size_t count, size_t size)
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

void *hal_realloc(void *ptr, size_t size)
{
    return realloc(ptr, size);
}

void hal_free(void *ptr)
{
    free(ptr);
}
