#ifndef VIBRATION_H
#define VIBRATION_H

/*
 * 振动开关驱动（滚珠/弹簧式振动传感器）。
 *
 * 参考 libdriver 风格：vibration_init / vibration_deinit / vibration_read
 * 引脚配置为下拉输入；read 内部做 VIBRATION_SAMPLES 次采样以捕捉短脉冲。
 */

#include "common/driver_io.h"
#include "vibration_register.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    iot_pin_t pin;
    bool initialized;
} vibration_handle_t;

/**
 * @brief  初始化：引脚配置为下拉输入
 * @param  pin  输出脚逻辑编号，0 视为无效
 */
int vibration_init(vibration_handle_t *handle, iot_pin_t pin);

int vibration_deinit(vibration_handle_t *handle);

/**
 * @brief  读取振动状态（多采样，任一有效即返回 true）
 * @param  vibrating  true 表示检测到振动
 */
int vibration_read(vibration_handle_t *handle, bool *vibrating);

#ifdef __cplusplus
}
#endif

#endif /* VIBRATION_H */
