#include "iot_bytes.h"
#include "iot_memory.h"

int iot_bytes_compare(const void *a, const void *b, iot_size_t len)
{
    return iot_memory_compare(a, b, len);
}

bool iot_bytes_equal(const void *a, const void *b, iot_size_t len)
{
    return iot_memory_compare(a, b, len) == 0;
}

void iot_bytes_copy(void *dst, const void *src, iot_size_t len)
{
    iot_memory_copy(dst, src, len);
}

void iot_bytes_fill(void *dst, uint8_t value, iot_size_t len)
{
    iot_memory_set(dst, value, len);
}

void iot_bytes_reverse(void *data, iot_size_t len)
{
    uint8_t *p = (uint8_t *)data;
    iot_size_t i;
    uint8_t t;

    if (data == NULL || len < 2) {
        return;
    }
    for (i = 0; i < len / 2; i++) {
        t = p[i];
        p[i] = p[len - 1 - i];
        p[len - 1 - i] = t;
    }
}