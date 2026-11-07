#ifndef SHA256_H
#define SHA256_H

/*
 * SHA-256 摘要算法。用于固件完整性校验、HMAC-SHA256 消息认证。
 *
 * 模块说明：
 *   - 流式接口：init -> 多次 update -> final 输出 32 字节摘要；
 *   - 一次性接口：compute 内部完成三步，适合小数据；
 *   - 上下文由调用者分配，模块无静态状态，可多实例并行。
 * 约束：digest 缓冲至少 32 字节；final 之后需重新 init 才能复用上下文。
 */

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* SHA-256 计算上下文：8 个链式状态字 + 累计比特数 + 未满一块的缓冲 */
typedef struct {
    uint32_t state[8];      /* 8 个链式状态字（大端语义） */
    uint64_t bit_count;     /* 已输入数据的总比特数 */
    uint8_t  buffer[64];    /* 未满一个分组的残留数据（64 字节） */
} sha256_t;

/*
 * 初始化上下文，写入标准初值并清零比特计数。
 * 参数：ctx 上下文指针，为 NULL 时直接返回。
 */
void sha256_init(sha256_t *ctx);

/*
 * 追加数据（可多次分片调用）。
 * 参数：ctx 上下文；data 输入数据；len 字节数。
 * 说明：ctx/data 为 NULL 或 len 为 0 时静默返回。
 */
void sha256_update(sha256_t *ctx, const void *data, uint32_t len);

/*
 * 结束计算并输出摘要。
 * 参数：ctx 上下文；digest 输出缓冲，长度至少 32 字节。
 * 说明：ctx/digest 为 NULL 时静默返回。
 */
void sha256_final(sha256_t *ctx, uint8_t digest[32]);

/* 一次性计算：data 为输入，len 为字节数，digest 为 32 字节输出缓冲 */
void sha256_compute(const void *data, uint32_t len, uint8_t digest[32]);

#ifdef __cplusplus
}
#endif

#endif /* SHA256_H */