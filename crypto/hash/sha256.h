#ifndef SHA256_H
#define SHA256_H

/*
 * SHA-256 摘要算法。用于固件完整性校验、HMAC-SHA256 消息认证。
 */

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t state[8];      /* 8 个 Hash 状态字 */
    uint64_t bit_count;     /* 已处理 bit 数 */
    uint8_t  buffer[64];    /* 未处理块 */
} sha256_t;

void sha256_init(sha256_t *ctx);
void sha256_update(sha256_t *ctx, const void *data, uint32_t len);
void sha256_final(sha256_t *ctx, uint8_t digest[32]);

void sha256_compute(const void *data, uint32_t len, uint8_t digest[32]);

#ifdef __cplusplus
}
#endif

#endif /* SHA256_H */