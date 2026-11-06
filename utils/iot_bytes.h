#ifndef IOT_BYTES_H
#define IOT_BYTES_H

/*
 * 字节数组操作。复用 iot_memory 原语。
 */

#include "iot_types.h"

#ifdef __cplusplus
extern "C" {
#endif

int  iot_bytes_compare(const void *a, const void *b, iot_size_t len);
bool iot_bytes_equal(const void *a, const void *b, iot_size_t len);
void iot_bytes_copy(void *dst, const void *src, iot_size_t len);
void iot_bytes_fill(void *dst, uint8_t value, iot_size_t len);
void iot_bytes_reverse(void *data, iot_size_t len);

#ifdef __cplusplus
}
#endif

#endif /* IOT_BYTES_H */