#ifndef YL69_H
#define YL69_H

/*
 * YL-69 土壤湿度传感器驱动（模拟量输入，输出湿度百分比）。
 *
 * 参考 libdriver 设计：
 *   yl69_init / yl69_deinit / yl69_read / yl69_read_raw
 *
 * 旧实现只回传原始电压；新实现内建干/湿端点并输出 0~100%。
 *
 * 硬件访问：直接调用 iot_adc_*，不包含 bus 目录头文件，不依赖任何 HAL。
 *
 * 用法：
 *   yl69_handle_t soil;
 *   yl69_init(&soil, &adc1, 11);
 *   float percent;
 *   yl69_read(&soil, &percent);
 *
 * 注意：电阻式探头长期通电会电解腐蚀，建议采样时供电、其余时间断电。
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
    float dry_mv;                /* 干燥端电压 mV（标定参数） */
    float wet_mv;                /* 饱和端电压 mV（标定参数） */
    bool initialized;
} yl69_handle_t;

/**
 * @brief  初始化驱动并配置 ADC 通道
 * @param  adc      已初始化的 ADC 句柄（不可为 NULL）
 * @param  channel  ADC 通道号
 * @return 0 成功；IOT_INVALID_PARAM 参数非法；IOT_IO_ERROR 通道配置失败
 */
int yl69_init(yl69_handle_t *handle, iot_adc_t *adc, uint8_t channel);

/** @brief 反初始化（不关闭 ADC） */
int yl69_deinit(yl69_handle_t *handle);

/**
 * @brief  读取土壤湿度
 * @param  percent  相对湿度 0~100%
 * @return 0 成功；IOT_NOT_READY 未初始化；IOT_IO_ERROR ADC 读取失败
 */
int yl69_read(yl69_handle_t *handle, float *percent);

/** @brief 读取 ADC 原始码值 */
int yl69_read_raw(yl69_handle_t *handle, uint32_t *raw);

/**
 * @brief  写入干/湿标定端点（mV），dry_mv 必须大于 wet_mv
 */
int yl69_set_calibration(yl69_handle_t *handle, float dry_mv, float wet_mv);

/** @brief 覆盖 ADC 参考电压与满量程码值 */
int yl69_set_reference(yl69_handle_t *handle, float vref_mv, uint32_t adc_max);

#ifdef __cplusplus
}
#endif

#endif /* YL69_H */
