#ifndef YL83_H
#define YL83_H

/*
 * YL-83 雨滴检测传感器驱动（模拟量输入，输出润湿百分比）。
 *
 * 参考 libdriver 设计：
 *   yl83_init / yl83_deinit / yl83_read / yl83_read_raw
 *
 * 输出语义：0% 干燥，100% 完全淋湿。旧实现只回传原始电压。
 *
 * 硬件访问：直接调用 iot_adc_*，不包含 bus 目录头文件，不依赖任何 HAL。
 *
 * 用法：
 *   yl83_handle_t rain;
 *   yl83_init(&rain, &adc1, 12);
 *   float wet;
 *   yl83_read(&rain, &wet);
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    hal_adc_t *adc;      /* 已初始化的 ADC 句柄 */
    uint8_t channel;             /* ADC 通道号 */
    float vref_mv;               /* ADC 参考电压 mV */
    uint32_t adc_max;            /* ADC 满量程码值 */
    float dry_mv;                /* 板面干燥电压 mV（标定参数） */
    float wet_mv;                /* 完全淋湿电压 mV（标定参数） */
    bool initialized;
} yl83_handle_t;

/**
 * @brief  初始化驱动并配置 ADC 通道
 * @param  adc      已初始化的 ADC 句柄（不可为 NULL）
 * @param  channel  ADC 通道号
 * @return 0 成功；HAL_INVALID_PARAM 参数非法；HAL_IO_ERROR 通道配置失败
 */
int yl83_init(yl83_handle_t *handle, hal_adc_t *adc, uint8_t channel);

/** @brief 反初始化（不关闭 ADC） */
int yl83_deinit(yl83_handle_t *handle);

/**
 * @brief  读取润湿程度
 * @param  percent  0~100%，0 为干燥
 * @return 0 成功；HAL_NOT_READY 未初始化；HAL_IO_ERROR ADC 读取失败
 */
int yl83_read(yl83_handle_t *handle, float *percent);

/** @brief 读取 ADC 原始码值 */
int yl83_read_raw(yl83_handle_t *handle, uint32_t *raw);

/**
 * @brief  写入干/湿标定端点（mV），dry_mv 必须大于 wet_mv
 */
int yl83_set_calibration(yl83_handle_t *handle, float dry_mv, float wet_mv);

/** @brief 覆盖 ADC 参考电压与满量程码值 */
int yl83_set_reference(yl83_handle_t *handle, float vref_mv, uint32_t adc_max);

#ifdef __cplusplus
}
#endif

#endif /* YL83_H */
