#ifndef MICS6814_H
#define MICS6814_H

/*
 * MiCS-6814 三通道气体传感器驱动（模拟量，ADC 电压输入）。
 *
 * 通道：RED(CO/还原性气体)、OX(NO2/氧化性气体)、NH3(氨气)。
 * 单位：mv -> mV；rs_ohm -> Ω；ratio -> Rs/R0；ppm_est -> ppm 估算。
 *
 * ADC 采样由上层完成（driver_io.h 未封装 ADC），本驱动只做电压 -> 浓度换算。
 * adc_mv 单位 mV，uint16_t 类型可表示 0..65535mV，但这只是类型上限，
 * **实际可用量程由所选 ADC 的 PGA/基准决定**（ads1115 默认 ±2.048V，
 * mcp3421/ads1110 固定 ±2.048V，超量程是码值饱和而非数值溢出）。
 * 典型来源：
 *   - 板载：drivers/voltage.c 的 voltage_read()（每通道一个句柄，12bit +
 *     均值滤波），其 vref_mv 必须与板上实际基准一致，否则 mV 系统性偏差
 *     会让 Rs/R0 与三通道比值同时漂移
 *   - 外部 ADC：adc_mv = (uint16_t)(volts * 1000.0f + 0.5f)
 * 真实关系 ppm = A*(Rs/R0)^B，需按 datasheet 标定，ppm_est 仅为线性近似。
 * 注意：OX 通道对氧化性气体呈现相反的电阻变化方向，浓度换算需按 datasheet
 * 曲线分别处理，不能用同一符号约定。
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MICS6814_CH_RED = 0,   /* CO 等还原性气体 */
    MICS6814_CH_OX,        /* NO2 等氧化性气体 */
    MICS6814_CH_NH3,       /* 氨气 */
    MICS6814_CH_MAX
} mics6814_channel_e;

typedef struct {
    uint16_t vc_mv;
    uint16_t rl_ohm[MICS6814_CH_MAX];
    uint32_t r0_ohm[MICS6814_CH_MAX];
    float ppm_scale[MICS6814_CH_MAX];
    bool initialized;
} mics6814_handle_t;

typedef struct {
    uint16_t mv[MICS6814_CH_MAX];        /* 原始 ADC 电压 mV */
    float rs_ohm[MICS6814_CH_MAX];       /* 传感器电阻 Ω */
    float ratio[MICS6814_CH_MAX];        /* Rs/R0，未标定为 0 */
    float ppm_est[MICS6814_CH_MAX];      /* ppm 估算（线性近似） */
    bool ppm_valid[MICS6814_CH_MAX];     /* false：该通道未标定 */
} mics6814_data_t;

/**
 * @brief  初始化驱动（不访问硬件）
 * @param  vc_mv  回路供电 mV，传 0 取默认值
 * @param  rl_ohm 三通道共用的负载电阻 Ω，传 0 取默认值
 */
int mics6814_init(mics6814_handle_t *handle, uint16_t vc_mv, uint16_t rl_ohm);

int mics6814_deinit(mics6814_handle_t *handle);

/** 单独设置某通道的负载电阻（各通道 RL 不同时使用） */
int mics6814_set_rl(mics6814_handle_t *handle, mics6814_channel_e ch, uint16_t rl_ohm);

/** 用洁净空气样本标定某通道 R0；clean_air_ratio 取自 datasheet 曲线 */
int mics6814_calibrate_r0(mics6814_handle_t *handle, mics6814_channel_e ch,
                          uint16_t adc_mv, float clean_air_ratio);

/** 设置某通道的 ppm 线性近似系数：ppm_est = ppm_scale * (Rs/R0) */
int mics6814_set_ppm_scale(mics6814_handle_t *handle, mics6814_channel_e ch,
                           float ppm_scale);

/**
 * @brief  由三通道 ADC 电压换算浓度
 * @param  adc_mv 指向 **恰好 3 个** uint16_t 的数组，单位 mV，顺序固定为
 *                [0]=MICS6814_CH_RED(CO)、[1]=MICS6814_CH_OX(NO2)、
 *                [2]=MICS6814_CH_NH3；驱动无法得知实际数组长度，
 *                调用方必须保证按此顺序传入 3 个元素。不可为 NULL。
 * @param  data   输出结果，不可为 NULL
 */
int mics6814_read(mics6814_handle_t *handle, const uint16_t *adc_mv,
                  mics6814_data_t *data);

#ifdef __cplusplus
}
#endif

#endif /* MICS6814_H */
