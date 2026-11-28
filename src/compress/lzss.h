#ifndef LZSS_H
#define LZSS_H

/*
 * lzss.h - LZSS 压缩/解压（无外部依赖）
 * ============================================================================
 *
 * 算法：LZSS（LZ77 + 标志位），窗口 4KB，最短匹配 3 字节，最长 18 字节。
 *
 * 数据格式（小端）：
 *   数据流分为若干组，每组以 1 字节控制字开始，bit0..bit7 依次对应组内
 *   8 个记号（低位在前）：
 *     bit = 1：后跟 1 字节字面量；
 *     bit = 0：后跟 2 字节匹配对
 *              (dist - 1) | ((len - 3) << 12)
 *              dist：回退距离 1..4096；len：匹配长度 3..18。
 *   最后一组不足 8 个记号时按实际个数结束，解码器在输入耗尽时停止。
 *
 * 特性与约束：
 *   - 编码在 src 上直接做窗口匹配，要求源缓冲完整可读，不需要滑动窗口副本；
 *   - 编码使用静态哈希表加速查找，非线程安全（同一时刻只允许一个编码调用）；
 *   - 解码不需要额外内存，可重入，支持 dist < len 的重叠复制；
 *   - 最坏情况下膨胀约 12.5%（每 8 字节多 1 字节控制字），
 *     可用 lzss_bound() 预留输出缓冲。
 *   - 格式不含原始长度/校验，调用方如需完整性校验请在外层叠加 CRC。
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 返回码 */
#define LZSS_OK           0
#define LZSS_ERR_PARAM   (-1)   /* 空指针或长度参数非法 */
#define LZSS_ERR_SPACE   (-2)   /* 输出缓冲不足 */
#define LZSS_ERR_DATA    (-3)   /* 压缩流损坏或截断 */

/* 压缩后最大可能字节数（含控制字），编码要求 dst_cap >= lzss_bound() */
uint32_t lzss_bound(uint32_t src_len);

/*
 * 压缩 src[0..src_len) 到 dst，长度为 *dst_len；src_len 为 0 时输出 0 字节。
 * 返回 LZSS_OK / LZSS_ERR_PARAM / LZSS_ERR_SPACE。
 */
int lzss_encode(const void *src, uint32_t src_len,
                void *dst, uint32_t dst_cap, uint32_t *dst_len);

/*
 * 解压 src[0..src_len) 到 dst，长度为 *dst_len。
 * dst_cap 为输出容量；输出超限返回 LZSS_ERR_SPACE，流损坏返回
 * LZSS_ERR_DATA（匹配距离超出已输出数据、匹配对不完整）。
 */
int lzss_decode(const void *src, uint32_t src_len,
                void *dst, uint32_t dst_cap, uint32_t *dst_len);

#ifdef __cplusplus
}
#endif

#endif /* LZSS_H */
