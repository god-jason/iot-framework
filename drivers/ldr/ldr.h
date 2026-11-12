#ifndef LDR_H
#define LDR_H

/*
 * LDR 光敏电阻驱动（模拟量输入，输出原始码值与 0~100% 相对亮度）。
 *
 * 参考 libdriver 设计：
 *   ldr_init / ldr_deinit / ldr_read / ldr_read_raw
 *
 * 旧实现只回传原始电压，并注释"亮度越大电压越低"——这与常见
 * "LDR 接上臂"的分压接法相反。新实现按上臂接法建模，端点可对调，
 * 不假设固定极性。
 *
 * 硬件访问：直接调用 iot_adc_*，不包含 bus 目录头文件，不依赖任何 HAL。
 *
 * 用法：
 *   ldr_handle_t ldr;
 *   ldr_init(&ldr, &adc1, 1);
 *   ldr_data_t d;
 *   ldr_read(&ldr, &d);     d.raw 原始码值，d.percent 0~100%
 */

#include "hal.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    hal_adc_t *adc;      /* 已初始化的 ADC 句柄 */
    uint8_t channel;             /* ADC 通道号 */
    float vref_mv;               /* ADC 参考电压 mV */
    uint32_t adc_max;            /* ADC 满量程码值 */
    float dark_mv;               /* 全暗端电压 mV（标定参数） */
    float bright_mv;             /* 强光端电压 mV（标定参数） */
    bool initialized;
} ldr_handle_t;

typedef struct {
    uint32_t raw;                /* ADC 原始码值 */
    float percent;               /* 相对亮度 0~100% */
} ldr_data_t;

/**
 * @brief  初始化驱动并配置 ADC 通道
 * @param  adc      已初始化的 ADC 句柄（不可为 NULL）
 * @param  channel  ADC 通道号
 * @return 0 成功；HAL_INVALID_PARAM 参数非法；HAL_IO_ERROR 通道配置失败
 */
int ldr_init(ldr_handle_t *handle, hal_adc_t *adc, uint8_t channel);

/** @brief 反初始化（不关闭 ADC） */
int ldr_deinit(ldr_handle_t *handle);

/**
 * @brief  读取亮度（同时给出原始码值）
 * @param  data  输出结果（不可为 NULL）
 * @return 0 成功；HAL_NOT_READY 未初始化；HAL_IO_ERROR ADC 读取失败
 */
int ldr_read(ldr_handle_t *handle, ldr_data_t *data);

/** @brief 读取 ADC 原始码值 */
int ldr_read_raw(ldr_handle_t *handle, uint32_t *raw);

/**
 * @brief  写入全暗/强光标定端点（mV）
 * @note   两者差值绝对值需大于 LDR_MIN_SPAN_MV；接反时对调两值即可
 */
int ldr_set_calibration(ldr_handle_t *handle, float dark_mv, float bright_mv);

/** @brief 覆盖 ADC 参考电压与满量程码值 */
int ldr_set_reference(ldr_handle_t *handle, float vref_mv, uint32_t adc_max);

#ifdef __cplusplus
}
#endif

#endif /* LDR_H */
