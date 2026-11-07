#ifndef TMP36_H
#define TMP36_H

/*
 * TMP36 温度传感器驱动（模拟量输入，输出 ℃）。
 *
 * 参考 libdriver 设计：
 *   tmp36_init / tmp36_deinit / tmp36_read / tmp36_read_raw
 *
 * 换算：T(℃) = (Vout(mV) - 500) / 10。旧实现公式正确，但无状态校验、
 * 无参考电压可配置；新实现统一句柄并允许覆盖 vref/adc_max。
 *
 * 硬件访问：直接调用 port_adc_*，不包含 bus 目录头文件，不依赖任何 HAL。
 *
 * 用法：
 *   tmp36_handle_t tmp;
 *   tmp36_init(&tmp, &adc1, 10);
 *   float celsius;
 *   tmp36_read(&tmp, &celsius);
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    port_adc_handle_t *adc;      /* 已初始化的 ADC 句柄 */
    uint8_t channel;             /* ADC 通道号 */
    float vref_mv;               /* ADC 参考电压 mV */
    uint32_t adc_max;            /* ADC 满量程码值 */
    bool initialized;
} tmp36_handle_t;

/**
 * @brief  初始化驱动并配置 ADC 通道
 * @param  adc      已初始化的 ADC 句柄（不可为 NULL）
 * @param  channel  ADC 通道号
 * @return 0 成功；PORT_INVALID_PARAM 参数非法；PORT_IO_ERROR 通道配置失败
 */
int tmp36_init(tmp36_handle_t *handle, port_adc_handle_t *adc, uint8_t channel);

/** @brief 反初始化（不关闭 ADC） */
int tmp36_deinit(tmp36_handle_t *handle);

/**
 * @brief  读取温度
 * @param  celsius  温度，单位 ℃（超出 -40~125℃ 时钳位并告警）
 * @return 0 成功；PORT_NOT_READY 未初始化；PORT_IO_ERROR ADC 读取失败
 */
int tmp36_read(tmp36_handle_t *handle, float *celsius);

/** @brief 读取 ADC 原始码值 */
int tmp36_read_raw(tmp36_handle_t *handle, uint32_t *raw);

/** @brief 覆盖 ADC 参考电压与满量程码值 */
int tmp36_set_reference(tmp36_handle_t *handle, float vref_mv, uint32_t adc_max);

#ifdef __cplusplus
}
#endif

#endif /* TMP36_H */
