#ifndef TCS3200_H
#define TCS3200_H

/*
 * TCS3200 颜色传感器驱动（RGB 频率输出）。
 *
 * 参考 libdriver 风格：tcs3200_init / tcs3200_deinit / tcs3200_read
 * 引脚：S0/S1 输出分频，S2/S3 滤波通道，OUT 频率输出（共 5 根）。
 * read 依次选择 R/G/B 滤波通道，在固定窗口内统计 OUT 上升沿，
 * 给出三通道原始频率（Hz）与归一化分量（最大通道为 1.0）。
 *
 * 说明：driver_io 层暂无输入捕获抽象，频率测量用 GPIO 轮询实现，
 * 采样窗内阻塞占用 CPU；高精度场合应改用定时器捕获。
 */

#include "common/driver_io.h"
#include "tcs3200_register.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 一次测量的结果：原始频率 + 归一化 RGB 分量（0.0 ~ 1.0） */
typedef struct {
    uint32_t r_hz;
    uint32_t g_hz;
    uint32_t b_hz;
    float r;
    float g;
    float b;
} tcs3200_color_t;

typedef struct {
    iot_pin_t s0;
    iot_pin_t s1;
    iot_pin_t s2;
    iot_pin_t s3;
    iot_pin_t out;
    uint32_t window_us;
    bool initialized;
} tcs3200_handle_t;

/**
 * @brief  初始化：S0~S3 推挽输出，OUT 输入（下拉）
 * @param  s0/s1/s2/s3/out  引脚逻辑编号，任一为 0 或彼此重复则视为无效
 */
int tcs3200_init(tcs3200_handle_t *handle, iot_pin_t s0, iot_pin_t s1,
                 iot_pin_t s2, iot_pin_t s3, iot_pin_t out);

int tcs3200_deinit(tcs3200_handle_t *handle);

/**
 * @brief  读取一次颜色（依次测 R/G/B，阻塞约 3 * (TCS3200_SETTLE_US + window)）
 * @param  color  输出结果，不可为 NULL
 */
int tcs3200_read(tcs3200_handle_t *handle, tcs3200_color_t *color);

#ifdef __cplusplus
}
#endif

#endif /* TCS3200_H */
