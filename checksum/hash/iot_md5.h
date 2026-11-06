#ifndef IOT_MD5_H
#define IOT_MD5_H

/*
 * MD5 摘要算法（RFC 1321）。用于旧协议兼容 / 简单完整性校验。
 * 不建议用于高安全场景（应使用 SHA-256）。
 */

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t state[4];      /* A B C D */
    uint64_t bit_count;     /* 已处理 bit 数 */
    uint8_t  buffer[64];    /* 未处理块 */
} iot_md5_t;

void iot_md5_init(iot_md5_t *ctx);
void iot_md5_update(iot_md5_t *ctx, const void *data, uint32_t len);
void iot_md5_final(iot_md5_t *ctx, uint8_t digest[16]);

/* 一次性计算 */
void iot_md5_compute(const void *data, uint32_t len, uint8_t digest[16]);

#ifdef __cplusplus
}
#endif

#endif /* IOT_MD5_H */