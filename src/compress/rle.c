/*
 * rle.c - 行程编码实现
 *
 * 编码：连续相同字节 >= 3 时输出重复组，其余攒成字面量组（最多 128 字节）。
 * 解码：按控制字展开重复组或复制字面量组，所有长度都做边界检查。
 */

#include "rle.h"

#include <string.h>

#define RLE_MAX_COUNT   128U

uint32_t rle_bound(uint32_t src_len)
{
    uint64_t n = (uint64_t)src_len;

    n += (n + 127U) / 128U;
    return (n > 0xFFFFFFFFULL) ? 0xFFFFFFFFU : (uint32_t)n;
}

/* 从 ip 起统计相同字节的数量（最多 RLE_MAX_COUNT） */
static uint32_t rle_run(const uint8_t *s, uint32_t len, uint32_t ip)
{
    uint32_t run = 1U;

    while ((ip + run) < len && run < RLE_MAX_COUNT &&
           s[ip + run] == s[ip]) {
        run++;
    }
    return run;
}

int rle_encode(const void *src, uint32_t src_len,
               void *dst, uint32_t dst_cap, uint32_t *dst_len)
{
    const uint8_t *s = (const uint8_t *)src;
    uint8_t *d = (uint8_t *)dst;
    uint32_t ip = 0U;
    uint32_t op = 0U;

    if ((src == NULL && src_len != 0U) || dst == NULL || dst_len == NULL) {
        return RLE_ERR_PARAM;
    }
    if (src_len == 0U) {
        *dst_len = 0U;
        return RLE_OK;
    }
    if (dst_cap < rle_bound(src_len)) {
        return RLE_ERR_SPACE;
    }

    while (ip < src_len) {
        uint32_t run = rle_run(s, src_len, ip);

        if (run >= 3U) {
            d[op++] = (uint8_t)(0x80U | (run - 1U));
            d[op++] = s[ip];
            ip += run;
            continue;
        }

        /* 字面量组：攒到出现 >=3 的重复或满 128 字节为止 */
        {
            uint32_t start = ip;
            uint32_t cnt = 0U;

            while (ip < src_len && cnt < RLE_MAX_COUNT) {
                uint32_t r = rle_run(s, src_len, ip);
                uint32_t take;

                if (r >= 3U) {
                    break;              /* 留给重复组 */
                }
                take = RLE_MAX_COUNT - cnt;
                if (take > r) {
                    take = r;
                }
                ip += take;
                cnt += take;
            }
            d[op++] = (uint8_t)(cnt - 1U);
            memcpy(&d[op], &s[start], cnt);
            op += cnt;
        }
    }

    *dst_len = op;
    return RLE_OK;
}

int rle_decode(const void *src, uint32_t src_len,
               void *dst, uint32_t dst_cap, uint32_t *dst_len)
{
    const uint8_t *s = (const uint8_t *)src;
    uint8_t *d = (uint8_t *)dst;
    uint32_t ip = 0U;
    uint32_t op = 0U;

    if ((src == NULL && src_len != 0U) || (dst == NULL && dst_cap != 0U) ||
        dst_len == NULL) {
        return RLE_ERR_PARAM;
    }

    while (ip < src_len) {
        uint8_t ctrl = s[ip++];
        uint32_t cnt = (uint32_t)(ctrl & 0x7FU) + 1U;

        if ((ctrl & 0x80U) != 0U) {
            uint8_t v;

            if (ip >= src_len) {
                return RLE_ERR_DATA;    /* 缺少重复值 */
            }
            v = s[ip++];
            if ((op + cnt) > dst_cap) {
                return RLE_ERR_SPACE;
            }
            memset(&d[op], v, cnt);
            op += cnt;
        } else {
            if ((ip + cnt) > src_len) {
                return RLE_ERR_DATA;    /* 字面量组被截断 */
            }
            if ((op + cnt) > dst_cap) {
                return RLE_ERR_SPACE;
            }
            memcpy(&d[op], &s[ip], cnt);
            ip += cnt;
            op += cnt;
        }
    }

    *dst_len = op;
    return RLE_OK;
}
