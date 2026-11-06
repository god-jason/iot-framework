#ifndef IOT_FILTER_H
#define IOT_FILTER_H

/*
 * 常用数字滤波器：均值 / 滑动平均 / 中值 / EMA / 一阶低通 / 一阶高通 / 限幅 / 卡尔曼 / 滞回。
 * 算法库独立于 Core 与 HAL；缓冲区由调用方提供，无动态内存。
 */

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 均值 / 滑动平均 ---- */
typedef struct {
    float *buffer;
    uint16_t size;
    uint16_t index;
    uint16_t count;
    float sum;
} iot_filter_mean_t;

int iot_filter_mean_init(iot_filter_mean_t *f, float *buffer, uint16_t size);
float iot_filter_mean_update(iot_filter_mean_t *f, float input);
void iot_filter_mean_reset(iot_filter_mean_t *f);

/* ---- 中值 ---- */
typedef struct {
    float *buffer;
    float *work;
    uint16_t size;
    uint16_t index;
    uint16_t count;
} iot_filter_median_t;

int iot_filter_median_init(iot_filter_median_t *f, float *buffer,
                           float *work, uint16_t size);
float iot_filter_median_update(iot_filter_median_t *f, float input);

/* ---- EMA ---- */
typedef struct {
    float alpha;
    float output;
    bool initialized;
} iot_filter_ema_t;

void iot_filter_ema_init(iot_filter_ema_t *f, float alpha);
float iot_filter_ema_update(iot_filter_ema_t *f, float input);
void iot_filter_ema_reset(iot_filter_ema_t *f);

/* ---- 一阶低通 ---- */
typedef struct {
    float alpha;
    float output;
    bool initialized;
} iot_filter_lowpass_t;

int iot_filter_lowpass_init(iot_filter_lowpass_t *f, float cutoff, float sample_rate);
float iot_filter_lowpass_update(iot_filter_lowpass_t *f, float input);

/* ---- 一阶高通 ---- */
typedef struct {
    float alpha;
    float prev_input;
    float prev_output;
    bool initialized;
} iot_filter_highpass_t;

int iot_filter_highpass_init(iot_filter_highpass_t *f, float cutoff, float sample_rate);
float iot_filter_highpass_update(iot_filter_highpass_t *f, float input);

/* ---- 限幅 ---- */
typedef struct {
    float max_change;
    float last_value;
    bool initialized;
} iot_filter_limit_t;

void iot_filter_limit_init(iot_filter_limit_t *f, float max_change);
float iot_filter_limit_update(iot_filter_limit_t *f, float input);

/* ---- 一维卡尔曼 ---- */
typedef struct {
    float q;
    float r;
    float x;
    float p;
    float k;
} iot_filter_kalman_t;

void iot_filter_kalman_init(iot_filter_kalman_t *f, float q, float r, float initial_value);
float iot_filter_kalman_update(iot_filter_kalman_t *f, float measurement);

/* ---- 滞回（开关量去抖） ---- */
typedef struct {
    float low;
    float high;
    bool state;
    bool initialized;
} iot_filter_hysteresis_t;

void iot_filter_hysteresis_init(iot_filter_hysteresis_t *f, float low, float high);
bool iot_filter_hysteresis_update(iot_filter_hysteresis_t *f, float input);

#ifdef __cplusplus
}
#endif

#endif /* IOT_FILTER_H */