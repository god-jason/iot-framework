#ifndef SOUND_LM393_H
#define SOUND_LM393_H

/*
 * 声音传感器（LM393 模块模拟输出）驱动。
 *
 * 参考 libdriver 设计：
 *   sound_lm393_init / _deinit / _read / _read_raw
 *
 * 输出：ADC 原始码值 + 0~100% 相对电平（Vout/Vref）。
 * 旧文件名/API 为 sound_init / sound_read，按目录名统一为 sound_lm393_*。
 *
 * 硬件访问：直接调用 port_adc_*，不包含 bus 目录头文件，不依赖任何 HAL。
 *
 * 用法：
 *   sound_lm393_handle_t mic;
 *   sound_lm393_init(&mic, &adc1, 9);
 *   sound_lm393_data_t d;
 *   sound_lm393_read(&mic, &d);    d.raw 原始码值，d.percent 0~100%
 *
 * 已知限制：单次采样无法代表响度（音频是交流信号）。需要峰值检测时
 * 由上层连续采样取 max-min；本驱动不阻塞总线。
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
} sound_lm393_handle_t;

typedef struct {
    uint32_t raw;                /* ADC 原始码值 */
    float percent;               /* 相对电平 0~100% */
} sound_lm393_data_t;

/**
 * @brief  初始化驱动并配置 ADC 通道
 * @param  adc      已初始化的 ADC 句柄（不可为 NULL）
 * @param  channel  ADC 通道号
 * @return 0 成功；PORT_INVALID_PARAM 参数非法；PORT_IO_ERROR 通道配置失败
 */
int sound_lm393_init(sound_lm393_handle_t *handle, port_adc_handle_t *adc, uint8_t channel);

/** @brief 反初始化（不关闭 ADC） */
int sound_lm393_deinit(sound_lm393_handle_t *handle);

/**
 * @brief  读取瞬时电平（同时给出原始码值）
 * @param  data  输出结果（不可为 NULL）
 * @return 0 成功；PORT_NOT_READY 未初始化；PORT_IO_ERROR ADC 读取失败
 */
int sound_lm393_read(sound_lm393_handle_t *handle, sound_lm393_data_t *data);

/** @brief 读取 ADC 原始码值 */
int sound_lm393_read_raw(sound_lm393_handle_t *handle, uint32_t *raw);

/** @brief 覆盖 ADC 参考电压与满量程码值 */
int sound_lm393_set_reference(sound_lm393_handle_t *handle, float vref_mv, uint32_t adc_max);

#ifdef __cplusplus
}
#endif

#endif /* SOUND_LM393_H */
