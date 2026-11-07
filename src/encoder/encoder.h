#ifndef ENCODER_H
#define ENCODER_H

/*
 * 编码器（正交/增量）：通过 ops 读取脉冲计数，并计算转速。
 * 底层可在 F4 定时器编码器模式或 GPIO 中断中实现计数。
 */

#include "iot_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int (*read_count)(void *ctx, int32_t *count);   /* 读累计脉冲（带符号） */
    int (*reset)(void *ctx);
    void *ctx;
} encoder_ops_t;

typedef struct {
    const encoder_ops_t *ops;

    int32_t count;       /* 最新计数 */
    int32_t last_count;  /* 上一次计数 */
    float speed;         /* 转速（脉冲/s） */
} encoder_t;

int encoder_init(encoder_t *enc, const encoder_ops_t *ops);

/* 读取当前计数 */
int encoder_read(encoder_t *enc, int32_t *count);

/* 更新并返回转速（脉冲/s）：(count - last_count) / dt */
int encoder_get_speed(encoder_t *enc, float dt, float *speed);

int encoder_reset(encoder_t *enc);

#ifdef __cplusplus
}
#endif

#endif /* ENCODER_H */