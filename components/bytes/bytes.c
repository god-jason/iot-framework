/*
 * bytes.c - 字节数组通用操作
 *
 * 模块职责：
 *   对裸内存块提供比较、判等、拷贝、填充、原地反转等基础操作，
 *   屏蔽 NULL 与零长度等边界情况，便于上层统一调用。
 *
 * 数据流与依赖：
 *   仅封装标准库 memcmp/memcpy/memset，无全局状态、无动态内存。
 *
 * 边界约束：
 *   - 所有函数对 NULL 指针和 len == 0 都做了短路处理，不会崩溃；
 *     但这属于“静默忽略”，不能用来替代上层的参数校验。
 *   - 调用者必须保证 dst/src 实际可访问长度不少于 len，本模块不做越界检查。
 */

#include "bytes.h"
#include <string.h>

/*
 * 按字典序比较两段内存。
 * 参数：a/b - 待比较内存；len - 比较字节数
 * 返回：<0 表示 a < b；0 表示相等；>0 表示 a > b
 * 注意：a 与 b 相同（同一指针）或 len == 0 时直接返回 0；
 *       a 为 NULL 返回 -1、b 为 NULL 返回 1，以保证 NULL 可比较。
 */
int bytes_compare(const void *a, const void *b, size_t len)
{
    if (a == b || len == 0) {
        return 0;
    }
    if (a == NULL) {
        return -1;
    }
    if (b == NULL) {
        return 1;
    }
    return memcmp(a, b, len);
}

/*
 * 判断两段内存内容是否完全相等。
 * 参数：a/b - 待比较内存；len - 比较字节数
 * 返回：true 相等；false 不等（含一方为 NULL 的情形）
 */
bool bytes_equal(const void *a, const void *b, size_t len)
{
    return bytes_compare(a, b, len) == 0;
}

/*
 * 拷贝 len 字节（不支持内存重叠，重叠场景请使用 memmove）。
 * 参数：dst - 目标；src - 源；len - 字节数
 * 返回：无
 * 注意：任一指针为 NULL 或 len == 0 时直接返回，不做任何操作。
 */
void bytes_copy(void *dst, const void *src, size_t len)
{
    if (dst == NULL || src == NULL || len == 0) {
        return;
    }
    memcpy(dst, src, len);
}

/*
 * 以指定字节值填充内存。
 * 参数：dst - 目标；value - 填充值；len - 字节数
 * 返回：无
 * 注意：dst 为 NULL 或 len == 0 时直接返回。
 */
void bytes_fill(void *dst, uint8_t value, size_t len)
{
    if (dst == NULL || len == 0) {
        return;
    }
    memset(dst, value, len);
}

/*
 * 原地反转字节顺序（常用于整数字节序调整）。
 * 参数：data - 数据首地址；len - 字节数
 * 返回：无
 * 注意：len < 2 时无需处理；只交换前 len/2 与后 len/2 个字节，
 *       因此奇数长度时中间字节保持原位。
 */
void bytes_reverse(void *data, size_t len)
{
    uint8_t *p = (uint8_t *)data;
    size_t i;
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
