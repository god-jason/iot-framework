#ifndef CAPSOIL_H
#define CAPSOIL_H

/*
 * 电容式土壤湿度传感器 v1.2 驱动（模拟量输入，输出湿度百分比）。
 *
 * 参考 libdriver 设计：
 *   capsoil_init / capsoil_deinit / capsoil_read / capsoil_read_raw
 *
 * 旧实现只回传原始电压（mV），由上层自行换算；新实现保留 raw 字段，
 * 同时按干/湿两个标定点给出 0~100% 体积含水率。
 *
 * 硬件访问：直接调用 iot_adc_*（drivers/common 未封装 ADC），
 * 不包含 bus 目录头文件，不依赖任何 HAL。
 *
 * 用法：
 *   capsoil_handle_t soil;
 *   capsoil_init(&soil, &adc1, 2);
 *   capsoil_set_calibration(&soil, 2580.0f, 1120.0f);   （实测端点）
 *   capsoil_data_t d;
 *   capsoil_read(&soil, &d);     d.percent 为 0~100%，d.raw 为原始码值
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
    float dry_mv;                /* 干燥端电压 mV（标定参数） */
    float wet_mv;                /* 饱和端电压 mV（标定参数） */
    bool initialized;
} capsoil_handle_t;

typedef struct {
    uint32_t raw;                /* ADC 原始码值 */
    float percent;               /* 相对含水率 0~100% */
} capsoil_data_t;

/**
 * @brief  初始化驱动并配置 ADC 通道
 * @param  adc      已初始化的 ADC 句柄（不可为 NULL）
 * @param  channel  ADC 通道号
 * @return 0 成功；HAL_INVALID_PARAM 参数非法；HAL_IO_ERROR 通道配置失败
 */
int capsoil_init(capsoil_handle_t *handle, hal_adc_t *adc, uint8_t channel);

/** @brief 反初始化（不关闭 ADC） */
int capsoil_deinit(capsoil_handle_t *handle);

/**
 * @brief  读取湿度百分比（同时给出原始码值）
 * @param  data  输出结果（不可为 NULL）
 * @return 0 成功；HAL_NOT_READY 未初始化；HAL_IO_ERROR ADC 读取失败
 */
int capsoil_read(capsoil_handle_t *handle, capsoil_data_t *data);

/** @brief 读取 ADC 原始码值 */
int capsoil_read_raw(capsoil_handle_t *handle, uint32_t *raw);

/**
 * @brief  写入干/湿标定端点（mV）
 * @note   两者差值需大于 CAPSOIL_MIN_SPAN_MV
 */
int capsoil_set_calibration(capsoil_handle_t *handle, float dry_mv, float wet_mv);

/** @brief 覆盖 ADC 参考电压与满量程码值 */
int capsoil_set_reference(capsoil_handle_t *handle, float vref_mv, uint32_t adc_max);

#ifdef __cplusplus
}
#endif

#endif /* CAPSOIL_H */
