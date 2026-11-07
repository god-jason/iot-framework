#ifndef LM35_H
#define LM35_H

/*
 * LM35 温度传感器驱动（模拟量输入，输出 ℃）。
 *
 * 参考 libdriver 设计：
 *   lm35_init / lm35_deinit / lm35_read / lm35_read_raw
 *
 * 换算：T(℃) = Vout(mV) / 10。旧实现公式正确，但把换算常数写死在 .c
 * 里且没有量程检查、没有状态校验；新实现统一句柄 + 校验 + 越界钳位。
 *
 * 硬件访问：直接调用 port_adc_*，不包含 bus 目录头文件，不依赖任何 HAL。
 *
 * 用法：
 *   lm35_handle_t lm35;
 *   lm35_init(&lm35, &adc1, 8);
 *   float celsius;
 *   lm35_read(&lm35, &celsius);
 *
 * 已知限制：LM35 不能测负温度；0mV 读数既可能是 0℃ 也可能是未接线，
 * 驱动按 0℃ 处理并在日志中告警。
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
} lm35_handle_t;

/**
 * @brief  初始化驱动并配置 ADC 通道
 * @param  adc      已初始化的 ADC 句柄（不可为 NULL）
 * @param  channel  ADC 通道号
 * @return 0 成功；PORT_INVALID_PARAM 参数非法；PORT_IO_ERROR 通道配置失败
 */
int lm35_init(lm35_handle_t *handle, port_adc_handle_t *adc, uint8_t channel);

/** @brief 反初始化（不关闭 ADC） */
int lm35_deinit(lm35_handle_t *handle);

/**
 * @brief  读取温度
 * @param  celsius  温度，单位 ℃（超出 0~150℃ 时钳位并告警）
 * @return 0 成功；PORT_NOT_READY 未初始化；PORT_IO_ERROR ADC 读取失败
 */
int lm35_read(lm35_handle_t *handle, float *celsius);

/** @brief 读取 ADC 原始码值 */
int lm35_read_raw(lm35_handle_t *handle, uint32_t *raw);

/** @brief 覆盖 ADC 参考电压与满量程码值 */
int lm35_set_reference(lm35_handle_t *handle, float vref_mv, uint32_t adc_max);

#ifdef __cplusplus
}
#endif

#endif /* LM35_H */
