/*
 * bytes.h - 字节数组通用操作接口
 *
 * 封装内存块比较、判等、拷贝、填充与反转；所有接口对 NULL/零长度安全，
 * 但不做越界检查。实现依赖标准库 memcmp/memcpy/memset。
 */

#ifndef BYTES_H
#define BYTES_H

/*
 * 字节数组操作。复用 ros_memory 原语。
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 按字典序比较；a 为 NULL 返回 -1，b 为 NULL 返回 1，同指针或 len==0 返回 0 */
int  bytes_compare(const void *a, const void *b, size_t len);
/* 内容完全相等返回 true（NULL 与合法指针比较结果为 false） */
bool bytes_equal(const void *a, const void *b, size_t len);
/* 拷贝 len 字节；不支持重叠区域，任一指针为 NULL 时不做操作 */
void bytes_copy(void *dst, const void *src, size_t len);
/* 以 value 填充 len 字节 */
void bytes_fill(void *dst, uint8_t value, size_t len);
/* 原地反转 len 字节的顺序（len < 2 时无操作） */
void bytes_reverse(void *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* BYTES_H */
