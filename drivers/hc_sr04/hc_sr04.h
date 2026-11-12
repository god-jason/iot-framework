#ifndef HC_SR04_H
#define HC_SR04_H

/*
 * HC-SR04 超声波测距模块（TRIG 触发 + ECHO 回响）驱动。
 *
 * 参考 libdriver 风格：
 *   hc_sr04_init / hc_sr04_deinit / hc_sr04_read
 * ECHO 高电平时长与距离成正比：distance_cm = echo_us / 58.3（声波往返）。
 *
 * 说明：本驱动使用 GPIO 轮询 + 微秒延时近似测量高电平时长，
 * 精度受调度/中断影响；高精度场合建议改用 hal_capture 输入捕获，
 * 需要时可在上层替换实现（见 .c 注释）。
 */

#include "hal.h"
#include "hc_sr04_register.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    hal_pin_t trig;
    hal_pin_t echo;
    uint32_t timeout_us;
    bool initialized;
} hc_sr04_handle_t;

/**
 * @brief  初始化模块引脚：TRIG 推挽输出（低）、ECHO 输入（下拉）
 * @param  trig  TRIG 引脚逻辑编号，0 表示无效
 * @param  echo  ECHO 引脚逻辑编号，0 表示无效
 */
int hc_sr04_init(hc_sr04_handle_t *handle, hal_pin_t trig, hal_pin_t echo);

int hc_sr04_deinit(hc_sr04_handle_t *handle);

/** 单次测量，返回距离（cm）；超出量程返回 HAL_OVERFLOW */
int hc_sr04_read(hc_sr04_handle_t *handle, float *distance_cm);

/** 单次测量，返回 ECHO 高电平时长（us） */
int hc_sr04_read_us(hc_sr04_handle_t *handle, uint32_t *echo_us);

/** 多次测量取平均（samples 为 1..HC_SR04_AVG_MAX_SAMPLES） */
int hc_sr04_read_average(hc_sr04_handle_t *handle, uint8_t samples, float *distance_cm);

/** 设置 ECHO 等待超时（us） */
int hc_sr04_set_timeout(hc_sr04_handle_t *handle, uint32_t timeout_us);

#ifdef __cplusplus
}
#endif

#endif /* HC_SR04_H */
