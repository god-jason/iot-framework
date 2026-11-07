#ifndef ACS712_H
#define ACS712_H

/*
 * ACS712 霍尔电流传感器驱动（模拟量输入，输出电流 A）。
 *
 * 参考 libdriver 设计：
 *   acs712_init / acs712_deinit / acs712_read / acs712_read_raw
 *
 * 硬件访问：drivers/common/driver_io.h 未封装 ADC，本驱动与 ntc 一致，
 * 直接调用 iot_adc_*（不包含 bus 目录头文件，不依赖任何 HAL）。
 *
 * 句柄保存灵敏度与零流点两项标定参数：
 *   - 5A 版换灵敏度 185mV/A、20A 版 100mV/A、30A 版 66mV/A
 *   - 零流点应在无负载时用 acs712_set_calibration 实测写入
 *
 * 用法：
 *   acs712_handle_t acs;
 *   acs712_init(&acs, &adc1, 0);
 *   acs712_set_calibration(&acs, ACS712_SENS_30A_MV_PER_A, 2495.0f);
 *   float amps;
 *   acs712_read(&acs, &amps);
 *
 * 注意：5V 供电时输出摆幅为 0.5V~4.5V，超过 3.3V ADC 量程，
 *       需分压或选用 5V 容忍的 ADC，否则读数在两端饱和。
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    iot_adc_t *adc;      /* 已初始化的 ADC 句柄 */
    uint8_t channel;             /* ADC 通道号 */
    float vref_mv;               /* ADC 参考电压 mV */
    uint32_t adc_max;            /* ADC 满量程码值 */
    float sens_mv_per_a;         /* 灵敏度 mV/A（标定参数） */
    float zero_mv;               /* 零电流输出 mV（标定参数） */
    bool initialized;
} acs712_handle_t;

/**
 * @brief  初始化驱动并配置 ADC 通道
 * @param  adc      已初始化的 ADC 句柄（不可为 NULL）
 * @param  channel  ADC 通道号
 * @return 0 成功；IOT_INVALID_PARAM 参数非法；IOT_IO_ERROR 通道配置失败
 */
int acs712_init(acs712_handle_t *handle, iot_adc_t *adc, uint8_t channel);

/** @brief 反初始化（不关闭 ADC） */
int acs712_deinit(acs712_handle_t *handle);

/**
 * @brief  读取电流
 * @param  amps  电流，单位 A，正负表示方向
 * @return 0 成功；IOT_NOT_READY 未初始化；IOT_IO_ERROR ADC 读取失败
 */
int acs712_read(acs712_handle_t *handle, float *amps);

/** @brief 读取 ADC 原始码值 */
int acs712_read_raw(acs712_handle_t *handle, uint32_t *raw);

/**
 * @brief  写入标定参数
 * @param  sens_mv_per_a  灵敏度 mV/A（>0）
 * @param  zero_mv        零电流时的输出电压 mV
 */
int acs712_set_calibration(acs712_handle_t *handle, float sens_mv_per_a, float zero_mv);

/**
 * @brief  覆盖 ADC 参考电压与满量程码值（默认取 iot_adc_get_max_value）
 */
int acs712_set_reference(acs712_handle_t *handle, float vref_mv, uint32_t adc_max);

#ifdef __cplusplus
}
#endif

#endif /* ACS712_H */
