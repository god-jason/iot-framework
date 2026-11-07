#ifndef IOT_MEMORY_H
#define IOT_MEMORY_H

/*
 * 内存原语。自包含实现，不依赖 libc 的动态内存分配。
 * 通过 iot_size_t 长度参数避免隐式溢出。
 */

#include "iot_types.h"

#ifdef __cplusplus
extern "C" {
#endif

void iot_memory_copy(void *dst, const void *src, iot_size_t len);
void iot_memory_move(void *dst, const void *src, iot_size_t len);
void iot_memory_set(void *dst, uint8_t value, iot_size_t len);
void iot_memory_zero(void *dst, iot_size_t len);
int  iot_memory_compare(const void *a, const void *b, iot_size_t len);

#ifdef __cplusplus
}
#endif

#endif /* IOT_MEMORY_H */