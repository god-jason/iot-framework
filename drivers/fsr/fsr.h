#ifndef FSR_H
#define FSR_H

/*
 * FSR 薄膜压力传感器驱动（模拟量输入，输出原始码值 + 估算压力）。
 *
 * 参考 libdriver 设计：
 *   fsr_init / fsr_deinit / fsr_read / fsr_read_raw
 *
 * 旧实现只回传原始电压；新实现给出 FSR 等效电阻与估算压力（g）。
 *
 * 重要：压力是**估算值**。FSR 的力-电阻曲线非线性且批次差异大，
 * 默认标定常数只保证量级正确，定量测量必须：
 *   1) 用已知砝码压住传感器，读出 fsr_data_t.resistance_ohm
 *   2) K = F_known(g) * R(Ω)，用 fsr_set_calibration 写入
 *
 * 硬件访问：直接调用 iot_adc_*，不包含 bus 目录头文件，不依赖任何 HAL。
 *
 * 用法：
 *   fsr_handle_t fsr;
 *   fsr_init(&fsr, &adc1, 6);
 *   fsr_data_t d;
 *   fsr_read(&fsr, &d);     d.raw / d.resistance_ohm / d.pressure_g
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
    uint32_t r_series_ohm;       /* 下臂固定电阻 Ω（标定参数） */
    float force_scale_g_ohm;     /* 力标定常数 K，单位 g·Ω（标定参数） */
    bool initialized;
} fsr_handle_t;

typedef struct {
    uint32_t raw;                /* ADC 原始码值 */
    float resistance_ohm;        /* FSR 等效电阻 Ω，无压力时为 1e9 */
    float pressure_g;            /* 估算压力，单位 g（需标定） */
} fsr_data_t;

/**
 * @brief  初始化驱动并配置 ADC 通道
 * @param  adc      已初始化的 ADC 句柄（不可为 NULL）
 * @param  channel  ADC 通道号
 * @return 0 成功；HAL_INVALID_PARAM 参数非法；HAL_IO_ERROR 通道配置失败
 */
int fsr_init(fsr_handle_t *handle, hal_adc_t *adc, uint8_t channel);

/** @brief 反初始化（不关闭 ADC） */
int fsr_deinit(fsr_handle_t *handle);

/**
 * @brief  读取压力
 * @param  data  输出结果（不可为 NULL）
 * @return 0 成功；HAL_NOT_READY 未初始化；HAL_IO_ERROR ADC 读取失败；
 *         HAL_HW_ERROR 输出贴顶（FSR 短路，物理上不可能）
 */
int fsr_read(fsr_handle_t *handle, fsr_data_t *data);

/** @brief 读取 ADC 原始码值 */
int fsr_read_raw(fsr_handle_t *handle, uint32_t *raw);

/**
 * @brief  写入标定参数
 * @param  r_series_ohm     下臂固定电阻（Ω，>0）
 * @param  force_scale_g_ohm 力常数 K（g·Ω，>0）
 */
int fsr_set_calibration(fsr_handle_t *handle, uint32_t r_series_ohm, float force_scale_g_ohm);

/** @brief 覆盖 ADC 参考电压与满量程码值 */
int fsr_set_reference(fsr_handle_t *handle, float vref_mv, uint32_t adc_max);

#ifdef __cplusplus
}
#endif

#endif /* FSR_H */
