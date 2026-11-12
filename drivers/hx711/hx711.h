#ifndef HX711_H
#define HX711_H

/*
 * HX711 24 位称重 ADC 驱动（2 线 GPIO：PD_SCK + DOUT，软件时序）。
 *
 * 参考 libdriver 风格：
 *   - hx711_init / hx711_deinit / hx711_read 为公开 API
 *   - 所有引脚操作经由 hal.h
 *
 * 用法：
 *   hx711_handle_t hx711;
 *   hx711_init(&hx711, HAL_PIN(0, 1), HAL_PIN(0, 2));
 *   hx711_tare(&hx711, 10);
 *   float weight;
 *   hx711_read(&hx711, &weight);
 */

#include "hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 下一次转换使用的通道与增益 */
typedef enum {
    HX711_GAIN_A_128 = 0,   /* 通道 A，增益 128（常用称重） */
    HX711_GAIN_B_32,        /* 通道 B，增益 32 */
    HX711_GAIN_A_64         /* 通道 A，增益 64 */
} hx711_gain_e;

typedef struct {
    hal_pin_t sck;         /* PD_SCK，主机输出 */
    hal_pin_t dout;        /* DOUT，芯片输出（低电平=就绪） */
    hx711_gain_e gain;      /* 当前通道/增益 */
    int32_t offset;         /* 零点（皮重）原始值 */
    float scale;            /* 换算系数：重量 = (raw - offset) / scale */
    bool initialized;
} hx711_handle_t;

/**
 * @brief  初始化（SCK 输出低电平，DOUT 输入上拉）
 * @param  handle  驱动句柄
 * @param  sck     PD_SCK 引脚
 * @param  dout    DOUT 引脚
 * @return 0 成功，负值为 HAL 状态码
 */
int hx711_init(hx711_handle_t *handle, hal_pin_t sck, hal_pin_t dout);

/** @brief 反初始化，释放引脚 */
int hx711_deinit(hx711_handle_t *handle);

/**
 * @brief  查询是否有新的转换结果（不消耗数据）
 * @return 1 就绪，0 忙，负值为错误码
 */
int hx711_is_ready(hx711_handle_t *handle);

/** @brief 设置下一次转换的通道与增益 */
int hx711_set_gain(hx711_handle_t *handle, hx711_gain_e gain);

/**
 * @brief  读取一次原始值（阻塞等待转换完成）
 * @param  raw  24 位补码扩展后的有符号值
 */
int hx711_read_raw(hx711_handle_t *handle, int32_t *raw);

/** @brief 连续读取 times 次并取平均 */
int hx711_read_average(hx711_handle_t *handle, uint8_t times, int32_t *raw);

/** @brief 设置零点原始值 */
int hx711_set_offset(hx711_handle_t *handle, int32_t offset);

/** @brief 设置换算系数（每单位重量对应的原始计数） */
int hx711_set_scale(hx711_handle_t *handle, float scale);

/** @brief 采集 times 次平均作为零点（皮重） */
int hx711_tare(hx711_handle_t *handle, uint8_t times);

/**
 * @brief  读取换算后的重量
 * @param  weight  重量 = (raw - offset) / scale
 */
int hx711_read(hx711_handle_t *handle, float *weight);

#ifdef __cplusplus
}
#endif

#endif /* HX711_H */
