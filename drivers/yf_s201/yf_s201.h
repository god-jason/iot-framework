#ifndef YF_S201_H
#define YF_S201_H

/*
 * YF-S201 霍尔水流量传感器驱动（方波脉冲输出）。
 *
 * 参考 libdriver 风格：yf_s201_init / yf_s201_deinit / yf_s201_read
 * read 在 handle->window_ms 采样窗内统计脉冲上升沿，输出频率（Hz）与流量（L/min）。
 *
 * 说明：driver_io 层暂无输入捕获/外部中断计数抽象，计数用 GPIO 轮询实现，
 * 采样窗内阻塞占用 CPU；高精度或非阻塞场合应改用定时器捕获 + 中断计数。
 */

#include "common/driver_io.h"
#include "yf_s201_register.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    iot_pin_t pin;
    float pulses_per_liter;   /* 每升脉冲数（默认 YF_S201_PULSES_PER_LITER_DEFAULT） */
    uint32_t window_ms;       /* 采样窗（默认 YF_S201_WINDOW_MS_DEFAULT） */
    bool initialized;
} yf_s201_handle_t;

/**
 * @brief  初始化：引脚配置为下拉输入
 * @param  pin  脉冲输出脚逻辑编号，0 视为无效
 */
int yf_s201_init(yf_s201_handle_t *handle, iot_pin_t pin);

int yf_s201_deinit(yf_s201_handle_t *handle);

/**
 * @brief  在一个采样窗内测量流量
 * @param  hz            脉冲频率（Hz），可为 NULL
 * @param  liters_per_min 流量（L/min），可为 NULL
 * @note   两者不可同时为 NULL
 */
int yf_s201_read(yf_s201_handle_t *handle, float *hz, float *liters_per_min);

/** 校准每升脉冲数（典型 450，范围 100~2000） */
int yf_s201_set_pulses_per_liter(yf_s201_handle_t *handle, float pulses_per_liter);

/** 设置采样窗（ms，YF_S201_WINDOW_MS_MIN ~ YF_S201_WINDOW_MS_MAX） */
int yf_s201_set_window(yf_s201_handle_t *handle, uint32_t window_ms);

#ifdef __cplusplus
}
#endif

#endif /* YF_S201_H */
