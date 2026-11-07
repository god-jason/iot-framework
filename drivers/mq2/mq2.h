#ifndef MQ2_H
#define MQ2_H

/*
 * MQ-2 可燃气体传感器驱动（模拟量，ADC 电压输入）。
 *
 * 单位约定：
 *   - mv     : 原始 ADC 电压，mV
 *   - rs_ohm : 探头等效电阻 Rs，Ω
 *   - ratio  : Rs/R0，无量纲（R0 未标定时为 0）
 *   - ppm_est: ppm 估算值，仅在调用过 mq2_set_ppm_scale 后有效（ppm_valid=true）
 *
 * 注意：MQ 系列为模拟量器件，硬件访问通过 driver_io.h 完成；由于
 * driver_io.h 未提供 ADC 封装，ADC 采样由上层完成（drivers/voltage.c 或
 * port_adc_read），本驱动只做电压 -> 电阻 -> 浓度的换算。
 *
 * 真实浓度关系为 ppm = A * (Rs/R0)^B（对数坐标下为直线），A、B 随器件与
 * 标定条件变化，需按 datasheet 标定；本驱动只提供 mv / Rs / Rs/R0 及可选的
 * 线性近似 ppm，不做高次幂运算以避免引入 libm。
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t vc_mv;        /* 回路供电 mV */
    uint16_t rl_ohm;       /* 负载电阻 Ω */
    uint32_t r0_ohm;       /* 洁净空气基准电阻 R0，0 表示未标定 */
    float ppm_scale;       /* ppm 线性近似系数，0 表示不输出 ppm */
    bool initialized;
} mq2_handle_t;

typedef struct {
    uint16_t mv;           /* 原始 ADC 电压 mV */
    float rs_ohm;          /* 探头电阻 Rs，Ω */
    float ratio;           /* Rs/R0，R0 未标定为 0 */
    float ppm_est;         /* ppm 估算值（线性近似） */
    bool ppm_valid;        /* false：未标定，ppm_est 无意义 */
} mq2_data_t;

/**
 * @brief  初始化驱动（不访问硬件）
 * @param  vc_mv   回路供电 mV，传 0 取 MQ2_DEFAULT_VC_MV
 * @param  rl_ohm  负载电阻 Ω，传 0 取 MQ2_DEFAULT_RL_OHM
 * @note   实际使用前需预热 MQ2_WARMUP_MS，并在洁净空气中标定 R0
 */
int mq2_init(mq2_handle_t *handle, uint16_t vc_mv, uint16_t rl_ohm);

int mq2_deinit(mq2_handle_t *handle);

/**
 * @brief  用洁净空气样本标定 R0
 * @param  adc_mv          洁净空气中的 ADC 电压 mV
 * @param  clean_air_ratio 洁净空气中 Rs/R0 的比值，取自 datasheet 曲线
 *                         （例如 MQ-2 约 9.8，需按所用 datasheet 核对）
 */
int mq2_calibrate_r0(mq2_handle_t *handle, uint16_t adc_mv, float clean_air_ratio);

/** 设置 ppm 线性近似系数：ppm_est = ppm_scale * (Rs/R0) */
int mq2_set_ppm_scale(mq2_handle_t *handle, float ppm_scale);

/**
 * @brief  由 ADC 电压换算气体浓度
 * @param  adc_mv 上层采样得到的电压 mV
 * @param  data   输出结果（不可为 NULL）
 * @retval PORT_INVALID_PARAM 句柄未初始化 / data 为 NULL / 电压越界
 */
int mq2_read(mq2_handle_t *handle, uint16_t adc_mv, mq2_data_t *data);

#ifdef __cplusplus
}
#endif

#endif /* MQ2_H */
