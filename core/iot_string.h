#ifndef IOT_STRING_H
#define IOT_STRING_H

/*
 * 字符串处理。自包含实现，不依赖动态内存。
 */

#include "iot_types.h"

#ifdef __cplusplus
extern "C" {
#endif

iot_size_t iot_string_length(const char *str);

char *iot_string_copy(char *dst, const char *src);
char *iot_string_ncopy(char *dst, const char *src, iot_size_t size);

int iot_string_compare(const char *a, const char *b);
int iot_string_ncompare(const char *a, const char *b, iot_size_t len);

char *iot_string_find(const char *str, const char *substr);
char *iot_string_find_char(const char *str, char ch);

int iot_string_split(char *str, char delimiter,
                     char **items, uint32_t capacity,
                     uint32_t *count);

int iot_string_to_int(const char *str, int32_t *value);
int iot_string_to_uint(const char *str, uint32_t *value);
int iot_string_to_float(const char *str, float *value);

#ifdef __cplusplus
}
#endif

#endif /* IOT_STRING_H */