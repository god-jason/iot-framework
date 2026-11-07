#ifndef MQ4_H
#define MQ4_H

/*
 * MQ-4 甲烷传感器驱动（模拟量，ADC 电压输入）。
 *
 * 单位：mv -> mV；rs_ohm -> Ω；ratio -> Rs/R0；ppm_est -> ppm 估算。
 * ADC 采样由上层完成（driver_io.h 未封装 ADC），本驱动只做电压 -> 浓度换算。
 * adc_mv 单位 mV，uint16_t 类型可表示 0..65535mV，但这只是类型上限，
 * **实际可用量程由所选 ADC 的 PGA/基准决定**：如 ads1115 默认 PGA=±2.048V、
 * mcp3421/ads1110 固定 ±2.048V（增益×1），超量程表现为码值饱和而非数值溢出，
 * 5V 模块需先调整 PGA/Vref。
 * 典型来源：
 *   - 板载：drivers/voltage.c 的 voltage_read()（12bit + 均值滤波），其
 *     vref_mv 必须与板上实际基准一致（外部 2.5V 基准却传 VDD 会让 mV
 *     系统性偏差，导致 Rs/R0 整体漂移）
 *   - 外部 ADC：adc_mv = (uint16_t)(volts * 1000.0f + 0.5f)
 * 预热见 MQ4_WARMUP_MS。
 * 真实关系 ppm = A*(Rs/R0)^B，需按 datasheet 标定，ppm_est 仅为线性近似。
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t vc_mv;
    uint16_t rl_ohm;
    uint32_t r0_ohm;
    float ppm_scale;
    bool initialized;
} mq4_handle_t;

typedef struct {
    uint16_t mv;
    float rs_ohm;
    float ratio;
    float ppm_est;
    bool ppm_valid;
} mq4_data_t;

int mq4_init(mq4_handle_t *handle, uint16_t vc_mv, uint16_t rl_ohm);
int mq4_deinit(mq4_handle_t *handle);
int mq4_calibrate_r0(mq4_handle_t *handle, uint16_t adc_mv, float clean_air_ratio);
int mq4_set_ppm_scale(mq4_handle_t *handle, float ppm_scale);
int mq4_read(mq4_handle_t *handle, uint16_t adc_mv, mq4_data_t *data);

#ifdef __cplusplus
}
#endif

#endif /* MQ4_H */
