#ifndef RLE_H
#define RLE_H

/*
 * rle.h - 行程编码（RLE，无外部依赖）
 * ============================================================================
 *
 * 数据格式：以组为单位，每组 1 字节控制字：
 *   bit7 = 1：重复组，低 7 位为 (count-1)，后跟 1 字节值，展开 count 次；
 *   bit7 = 0：字面量组，低 7 位为 (count-1)，后跟 count 字节原文。
 * count 范围 1..128。
 *
 * 适用：连续重复字节多的数据（位图、日志、传感器缓存）。
 * 最坏膨胀约 0.8%（每 128 字节多 1 字节控制字）。
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RLE_OK           0
#define RLE_ERR_PARAM   (-1)
#define RLE_ERR_SPACE   (-2)    /* 输出缓冲不足 */
#define RLE_ERR_DATA    (-3)    /* 压缩流损坏或截断 */

/* 压缩后最大字节数（含控制字） */
uint32_t rle_bound(uint32_t src_len);

/* 压缩：返回 RLE_OK / RLE_ERR_PARAM / RLE_ERR_SPACE */
int rle_encode(const void *src, uint32_t src_len,
               void *dst, uint32_t dst_cap, uint32_t *dst_len);

/* 解压：返回 RLE_OK / RLE_ERR_PARAM / RLE_ERR_SPACE / RLE_ERR_DATA */
int rle_decode(const void *src, uint32_t src_len,
               void *dst, uint32_t dst_cap, uint32_t *dst_len);

#ifdef __cplusplus
}
#endif

#endif /* RLE_H */
