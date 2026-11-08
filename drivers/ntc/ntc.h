#ifndef NTC_H
#define NTC_H

/*
 * NTC 热敏电阻驱动（ADC 分压 + B 值公式，输出 ℃）。
 *
 * 参考 libdriver 设计：
 *   ntc_init / ntc_deinit / ntc_read
 *
 * 说明：drivers/common/driver_io.h 未提供 ADC 抽象，本驱动与其他 ADC
 *       器件（lm35 / da267）一致，直接使用 iot_adc_* 接口；
 *       不使用任何 HAL，也不包含 bus 目录下的总线头文件。
 *
 * 用法：
 *   ntc_handle_t ntc;
 *   ntc_init(&ntc, &adc1, 0, NTC_DEFAULT_R_SERIES, NTC_DEFAULT_R25,
 *            NTC_DEFAULT_BETA, NTC_DEFAULT_VREF_MV, NTC_DEFAULT_ADC_MAX);
 *   float t;
 *   ntc_read(&ntc, &t);
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    hal_adc_t *adc;   /* 已初始化的 ADC 句柄 */
    uint8_t channel;          /* ADC 通道号 */
    uint32_t r_series;        /* 分压串联电阻，单位 Ω */
    float r25;                /* 25℃ 标称电阻，单位 Ω */
    float beta;               /* B 值，单位 K */
    float vref_mv;            /* ADC 参考电压，单位 mV */
    uint32_t adc_max;         /* ADC 满量程码值（12 位为 4095） */
    bool initialized;
} ntc_handle_t;

/**
 * @brief  初始化 NTC 驱动
 * @param  adc       已初始化的 ADC 句柄（不可为 NULL）
 * @param  channel   ADC 通道号
 * @param  r_series  分压串联电阻（Ω，>0）
 * @param  r25       25℃ 标称电阻（Ω，>0）
 * @param  beta      B 值（K，>0）
 * @param  vref_mv   参考电压（mV，>0）
 * @param  adc_max   满量程码值（>0）
 */
int ntc_init(ntc_handle_t *handle, hal_adc_t *adc, uint8_t channel,
             uint32_t r_series, float r25, float beta,
             float vref_mv, uint32_t adc_max);

/** @brief 反初始化（不关闭 ADC，句柄由调用方管理） */
int ntc_deinit(ntc_handle_t *handle);

/**
 * @brief  读取温度
 * @param  temperature  温度，单位 ℃
 * @return 0 成功；HAL_HW_ERROR 表示 v=0（短路）或开路/超量程
 */
int ntc_read(ntc_handle_t *handle, float *temperature);

/** @brief 读取换算后的 NTC 电阻（Ω），便于上层自检 */
int ntc_read_resistance(ntc_handle_t *handle, float *ohms);

#ifdef __cplusplus
}
#endif

#endif /* NTC_H */
