/*
 * lzss.c - LZSS 压缩/解压实现
 *
 * 编码：顺序扫描源数据，用哈希表记录最近一次出现的位置（双字节前缀），
 * 命中后直接与源缓冲比较求最长匹配（允许自重叠），命中长度 >= 3 时输出
 * 匹配对，否则输出字面量。每 8 个记号写一个控制字（先占位后回填）。
 *
 * 解码：按控制字的位逐个还原字面量或匹配对；匹配复制按字节进行，天然
 * 支持 dist < len 的重叠展开。
 */

#include "lzss.h"

#include <string.h>

#define LZSS_WINDOW      4096U   /* 回退窗口（12 位距离） */
#define LZSS_MIN_MATCH   3U      /* 最短匹配长度 */
#define LZSS_MAX_MATCH   18U     /* 最长匹配长度（4 位长度字段） */

/* 哈希表位数：越大压缩率略好、内存多；默认 256 项（1KB） */
#ifndef LZSS_HASH_BITS
#define LZSS_HASH_BITS   8U
#endif
#if (LZSS_HASH_BITS < 1) || (LZSS_HASH_BITS > 16)
#error "LZSS_HASH_BITS must be 1..16"
#endif
#define LZSS_HASH_SIZE   (1U << LZSS_HASH_BITS)

/* 位置 + 1，0 表示空；编码非重入 */
static uint32_t s_hash[LZSS_HASH_SIZE];

static uint32_t lz_hash(const uint8_t *p)
{
    uint32_t v = (uint32_t)p[0] | ((uint32_t)p[1] << 8);

    return (v * 2654435761U) >> (32U - LZSS_HASH_BITS);
}

uint32_t lzss_bound(uint32_t src_len)
{
    uint64_t n = (uint64_t)src_len;

    n += (n + 7U) / 8U;
    return (n > 0xFFFFFFFFULL) ? 0xFFFFFFFFU : (uint32_t)n;
}

int lzss_encode(const void *src, uint32_t src_len,
                void *dst, uint32_t dst_cap, uint32_t *dst_len)
{
    const uint8_t *s = (const uint8_t *)src;
    uint8_t *d = (uint8_t *)dst;
    uint32_t ip = 0U;
    uint32_t op = 0U;

    if ((src == NULL && src_len != 0U) || dst == NULL || dst_len == NULL) {
        return LZSS_ERR_PARAM;
    }
    if (src_len == 0U) {
        *dst_len = 0U;
        return LZSS_OK;
    }
    if (dst_cap < lzss_bound(src_len)) {
        return LZSS_ERR_SPACE;
    }

    memset(s_hash, 0, sizeof(s_hash));

    while (ip < src_len) {
        uint32_t ctrl_pos = op++;
        uint8_t flags = 0U;
        uint32_t bit;

        for (bit = 0U; bit < 8U && ip < src_len; bit++) {
            uint32_t best_len = 0U;
            uint32_t best_dist = 0U;

            if ((ip + 1U) < src_len) {      /* 至少 2 字节才能建哈希 */
                uint32_t h = lz_hash(&s[ip]);
                uint32_t ref = s_hash[h];

                s_hash[h] = ip + 1U;
                if (ref != 0U) {
                    uint32_t cand = ref - 1U;
                    uint32_t dist = ip - cand;

                    if (dist >= 1U && dist <= LZSS_WINDOW) {
                        uint32_t max_len = src_len - ip;
                        uint32_t len = 0U;

                        if (max_len > LZSS_MAX_MATCH) {
                            max_len = LZSS_MAX_MATCH;
                        }
                        /* 允许 cand + len 越过 ip（源数据完整可读） */
                        while (len < max_len &&
                               s[cand + len] == s[ip + len]) {
                            len++;
                        }
                        if (len >= LZSS_MIN_MATCH) {
                            best_len = len;
                            best_dist = dist;
                        }
                    }
                }
            }

            if (best_len >= LZSS_MIN_MATCH) {
                uint16_t v = (uint16_t)(best_dist - 1U) |
                             (uint16_t)((best_len - LZSS_MIN_MATCH) << 12);
                uint32_t k;

                d[op++] = (uint8_t)(v & 0xFFU);
                d[op++] = (uint8_t)(v >> 8);
                /* 把匹配区间内的位置也登记进哈希，提升后续命中率 */
                for (k = 1U; k < best_len && (ip + k + 1U) < src_len; k++) {
                    s_hash[lz_hash(&s[ip + k])] = ip + k + 1U;
                }
                ip += best_len;
            } else {
                flags |= (uint8_t)(1U << bit);
                d[op++] = s[ip++];
            }
        }
        d[ctrl_pos] = flags;
    }

    *dst_len = op;
    return LZSS_OK;
}

int lzss_decode(const void *src, uint32_t src_len,
                void *dst, uint32_t dst_cap, uint32_t *dst_len)
{
    const uint8_t *s = (const uint8_t *)src;
    uint8_t *d = (uint8_t *)dst;
    uint32_t ip = 0U;
    uint32_t op = 0U;

    if ((src == NULL && src_len != 0U) || (dst == NULL && dst_cap != 0U) ||
        dst_len == NULL) {
        return LZSS_ERR_PARAM;
    }

    while (ip < src_len) {
        uint8_t flags = s[ip++];
        uint32_t bit;

        for (bit = 0U; bit < 8U && ip < src_len; bit++) {
            if ((flags & (uint8_t)(1U << bit)) != 0U) {
                /* 字面量 */
                if (op >= dst_cap) {
                    return LZSS_ERR_SPACE;
                }
                d[op++] = s[ip++];
            } else {
                uint16_t v;
                uint32_t dist;
                uint32_t len;
                uint32_t i;

                if ((ip + 1U) >= src_len) {
                    return LZSS_ERR_DATA;   /* 匹配对不完整 */
                }
                v = (uint16_t)((uint16_t)s[ip] |
                               ((uint16_t)s[ip + 1U] << 8));
                ip += 2U;
                dist = (uint32_t)(v & 0x0FFFU) + 1U;
                len = (uint32_t)((v >> 12) & 0x0FU) + LZSS_MIN_MATCH;

                if (dist > op) {
                    return LZSS_ERR_DATA;   /* 引用尚未输出的数据 */
                }
                if ((op + len) > dst_cap) {
                    return LZSS_ERR_SPACE;
                }
                for (i = 0U; i < len; i++) {
                    d[op] = d[op - dist];       /* 支持 dist < len 重叠 */
                    op++;
                }
            }
        }
    }

    *dst_len = op;
    return LZSS_OK;
}
