#ifndef FLEX_H
#define FLEX_H

/*
 * 弯曲传感器驱动（模拟量输入，输出原始码值与 0~100 归一化弯曲度）。
 *
 * 参考 libdriver 设计：
 *   flex_init / flex_deinit / flex_read / flex_read_raw
 *
 * 归一化值 0 对应平直姿态标定点，100 对应最大弯曲标定点；
 * 它不是角度值——如需角度必须逐点标定曲线（非线性）。
 *
 * 硬件访问：直接调用 port_adc_*，不包含 bus 目录头文件，不依赖任何 HAL。
 *
 * 用法：
 *   flex_handle_t bend;
 *   flex_init(&bend, &adc1, 5);
 *   flex_set_calibration(&bend, 880.0f, 2450.0f);   （实测端点）
 *   flex_data_t d;
 *   flex_read(&bend, &d);    d.raw 原始码值，d.normalized 0~100
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
    float flat_mv;               /* 平直姿态电压 mV（标定参数） */
    float bent_mv;               /* 最大弯曲电压 mV（标定参数） */
    bool initialized;
} flex_handle_t;

typedef struct {
    uint32_t raw;                /* ADC 原始码值 */
    float normalized;            /* 归一化弯曲度 0~100 */
} flex_data_t;

/**
 * @brief  初始化驱动并配置 ADC 通道
 * @param  adc      已初始化的 ADC 句柄（不可为 NULL）
 * @param  channel  ADC 通道号
 * @return 0 成功；PORT_INVALID_PARAM 参数非法；PORT_IO_ERROR 通道配置失败
 */
int flex_init(flex_handle_t *handle, port_adc_handle_t *adc, uint8_t channel);

/** @brief 反初始化（不关闭 ADC） */
int flex_deinit(flex_handle_t *handle);

/**
 * @brief  读取弯曲度（同时给出原始码值）
 * @param  data  输出结果（不可为 NULL）
 * @return 0 成功；PORT_NOT_READY 未初始化；PORT_IO_ERROR ADC 读取失败
 */
int flex_read(flex_handle_t *handle, flex_data_t *data);

/** @brief 读取 ADC 原始码值 */
int flex_read_raw(flex_handle_t *handle, uint32_t *raw);

/**
 * @brief  写入平直/弯曲标定端点（mV）
 * @note   flat_mv 与 bent_mv 的差值绝对值需大于 FLEX_MIN_SPAN_MV
 */
int flex_set_calibration(flex_handle_t *handle, float flat_mv, float bent_mv);

/** @brief 覆盖 ADC 参考电压与满量程码值 */
int flex_set_reference(flex_handle_t *handle, float vref_mv, uint32_t adc_max);

#ifdef __cplusplus
}
#endif

#endif /* FLEX_H */
