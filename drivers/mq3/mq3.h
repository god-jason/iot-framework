#ifndef MQ3_H
#define MQ3_H

/*
 * MQ-3 酒精传感器驱动（模拟量，ADC 电压输入）。
 *
 * 单位：mv -> mV；rs_ohm -> Ω；ratio -> Rs/R0（无量纲）；ppm_est -> ppm 估算。
 * ADC 采样由上层完成（driver_io.h 未封装 ADC），本驱动只做电压 -> 浓度换算。
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
} mq3_handle_t;

typedef struct {
    uint16_t mv;
    float rs_ohm;
    float ratio;
    float ppm_est;
    bool ppm_valid;
} mq3_data_t;

/* vc_mv / rl_ohm 传 0 取默认值；使用前需预热，并在洁净空气中标定 R0 */
int mq3_init(mq3_handle_t *handle, uint16_t vc_mv, uint16_t rl_ohm);
int mq3_deinit(mq3_handle_t *handle);

/* clean_air_ratio：洁净空气中 Rs/R0 的比值，取自 datasheet 曲线 */
int mq3_calibrate_r0(mq3_handle_t *handle, uint16_t adc_mv, float clean_air_ratio);

/* ppm_est = ppm_scale * (Rs/R0) */
int mq3_set_ppm_scale(mq3_handle_t *handle, float ppm_scale);

/* adc_mv：上层采样得到的电压 mV */
int mq3_read(mq3_handle_t *handle, uint16_t adc_mv, mq3_data_t *data);

#ifdef __cplusplus
}
#endif

#endif /* MQ3_H */
