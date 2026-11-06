#include "iot_memory.h"

void iot_memory_copy(void *dst, const void *src, iot_size_t len)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    iot_size_t i;

    if (dst == NULL || src == NULL || len == 0 || dst == src) {
        return;
    }
    for (i = 0; i < len; i++) {
        d[i] = s[i];
    }
}

void iot_memory_move(void *dst, const void *src, iot_size_t len)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    iot_size_t i;

    if (dst == NULL || src == NULL || len == 0 || dst == src) {
        return;
    }
    /* 重叠安全：向前重叠从后往前，其余从前往后 */
    if (d > s && d < (s + len)) {
        for (i = len; i > 0; i--) {
            d[i - 1] = s[i - 1];
        }
    } else {
        for (i = 0; i < len; i++) {
            d[i] = s[i];
        }
    }
}

void iot_memory_set(void *dst, uint8_t value, iot_size_t len)
{
    uint8_t *d = (uint8_t *)dst;
    iot_size_t i;

    if (dst == NULL || len == 0) {
        return;
    }
    for (i = 0; i < len; i++) {
        d[i] = value;
    }
}

void iot_memory_zero(void *dst, iot_size_t len)
{
    iot_memory_set(dst, 0x00u, len);
}

int iot_memory_compare(const void *a, const void *b, iot_size_t len)
{
    const uint8_t *pa = (const uint8_t *)a;
    const uint8_t *pb = (const uint8_t *)b;
    iot_size_t i;

    if (a == b || len == 0) {
        return 0;
    }
    if (a == NULL) {
        return -1;
    }
    if (b == NULL) {
        return 1;
    }
    for (i = 0; i < len; i++) {
        if (pa[i] != pb[i]) {
            return (pa[i] < pb[i]) ? -1 : 1;
        }
    }
    return 0;
}