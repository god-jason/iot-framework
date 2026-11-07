#ifndef ENCODER_H
#define ENCODER_H

/*
 * 编码器（正交/增量）：通过 ops 读取脉冲计数，并计算转速。
 * 底层可在 F4 定时器编码器模式或 GPIO 中断中实现计数。
 *
 * 模块说明：
 *   - count 为带符号累计脉冲数，符号表示旋转方向；
 *   - 转速通过两次采样的差分除以时间间隔得到，单位脉冲/秒；
 *   - 模块本身不读取硬件，全部操作由 encoder_ops_t 注入。
 * 使用约束：
 *   - read_count 回调必须实现，其余回调可选；
 *   - 计算速度时 dt 必须大于 0，采样间隔需稳定以保证精度。
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 底层编码器操作表 */
typedef struct {
    int (*read_count)(void *ctx, int32_t *count);   /* 读累计脉冲（带符号），必须实现 */
    int (*reset)(void *ctx);                        /* 硬件计数复位，可选 */
    void *ctx;                                      /* 驱动私有上下文 */
} encoder_ops_t;

/* 编码器对象状态 */
typedef struct {
    const encoder_ops_t *ops;   /* 底层操作表 */

    int32_t count;       /* 最新一次读取到的累计计数 */
    int32_t last_count;  /* 上一次参与差分计算的计数 */
    float speed;         /* 最近一次计算的转速（脉冲/秒） */
} encoder_t;

/*
 * 初始化编码器。
 * 参数：enc 对象；ops 操作表（read_count 必须非 NULL）。
 * 返回：0 成功；参数非法返回 -1。
 */
int encoder_init(encoder_t *enc, const encoder_ops_t *ops);

/* 读取当前计数：成功返回 0 并写入 *count，失败返回 -1 */
int encoder_read(encoder_t *enc, int32_t *count);

/* 更新并返回转速（脉冲/s）：(count - last_count) / dt；dt 必须 > 0 */
int encoder_get_speed(encoder_t *enc, float dt, float *speed);

/*
 * 复位编码器：同步差分基准并调用可选的硬件 reset。
 * 参数：enc 对象。
 * 返回：0 成功；enc 为 NULL 返回 -1。
 */
int encoder_reset(encoder_t *enc);

#ifdef __cplusplus
}
#endif

#endif /* ENCODER_H */