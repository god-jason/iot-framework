#ifndef MD5_H
#define MD5_H

/*
 * MD5 摘要算法（RFC 1321）。用于旧协议兼容 / 简单完整性校验。
 * 不建议用于高安全场景（应使用 SHA-256）。
 *
 * 模块说明：
 *   - 流式接口：先 init，再多次 update，最后 final 输出 16 字节摘要；
 *   - 一次性接口：compute 内部自动完成上述三步，适合小数据；
 *   - 上下文完全由调用者分配，模块内无静态状态，可多实例并行。
 * 约束：digest 缓冲至少 16 字节；final 之后上下文不可复用，需重新 init。
 */

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* MD5 计算上下文：保存链式状态、累计比特数与未满一块的缓冲 */
typedef struct {
    uint32_t state[4];      /* 4 个链式状态字 A B C D */
    uint64_t bit_count;     /* 已输入数据的总比特数（用于填充长度字段） */
    uint8_t  buffer[64];    /* 未满一个分组的残留数据（64 字节） */
} md5_t;

/*
 * 初始化上下文，写入标准初值并清零比特计数。
 * 参数：ctx 上下文指针，为 NULL 时直接返回。
 */
void md5_init(md5_t *ctx);

/*
 * 追加数据（可多次分片调用）。
 * 参数：ctx 上下文；data 输入数据；len 字节数。
 * 说明：ctx/data 为 NULL 或 len 为 0 时静默返回。
 */
void md5_update(md5_t *ctx, const void *data, uint32_t len);

/*
 * 结束计算并输出摘要。
 * 参数：ctx 上下文；digest 输出缓冲，长度至少 16 字节。
 * 说明：ctx/digest 为 NULL 时静默返回。
 */
void md5_final(md5_t *ctx, uint8_t digest[16]);

/* 一次性计算：data 为输入，len 为字节数，digest 为 16 字节输出缓冲 */
void md5_compute(const void *data, uint32_t len, uint8_t digest[16]);

#ifdef __cplusplus
}
#endif

#endif /* MD5_H */