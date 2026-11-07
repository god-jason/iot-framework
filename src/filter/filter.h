#ifndef FILTER_H
#define FILTER_H

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
} filter_mean_t;

int filter_mean_init(filter_mean_t *f, float *buffer, uint16_t size);
float filter_mean_update(filter_mean_t *f, float input);
void filter_mean_reset(filter_mean_t *f);

/* ---- 中值 ---- */
typedef struct {
    float *buffer;
    float *work;
    uint16_t size;
    uint16_t index;
    uint16_t count;
} filter_median_t;

int filter_median_init(filter_median_t *f, float *buffer,
                           float *work, uint16_t size);
float filter_median_update(filter_median_t *f, float input);

/* ---- EMA ---- */
typedef struct {
    float alpha;
    float output;
    bool initialized;
} filter_ema_t;

void filter_ema_init(filter_ema_t *f, float alpha);
float filter_ema_update(filter_ema_t *f, float input);
void filter_ema_reset(filter_ema_t *f);

/* ---- 一阶低通 ---- */
typedef struct {
    float alpha;
    float output;
    bool initialized;
} filter_lowpass_t;

int filter_lowpass_init(filter_lowpass_t *f, float cutoff, float sample_rate);
float filter_lowpass_update(filter_lowpass_t *f, float input);

/* ---- 一阶高通 ---- */
typedef struct {
    float alpha;
    float prev_input;
    float prev_output;
    bool initialized;
} filter_highpass_t;

int filter_highpass_init(filter_highpass_t *f, float cutoff, float sample_rate);
float filter_highpass_update(filter_highpass_t *f, float input);

/* ---- 限幅 ---- */
typedef struct {
    float max_change;
    float last_value;
    bool initialized;
} filter_limit_t;

void filter_limit_init(filter_limit_t *f, float max_change);
float filter_limit_update(filter_limit_t *f, float input);

/* ---- 一维卡尔曼 ---- */
typedef struct {
    float q;
    float r;
    float x;
    float p;
    float k;
} filter_kalman_t;

void filter_kalman_init(filter_kalman_t *f, float q, float r, float initial_value);
float filter_kalman_update(filter_kalman_t *f, float measurement);

/* ---- 滞回（开关量去抖） ---- */
typedef struct {
    float low;
    float high;
    bool state;
    bool initialized;
} filter_hysteresis_t;

void filter_hysteresis_init(filter_hysteresis_t *f, float low, float high);
bool filter_hysteresis_update(filter_hysteresis_t *f, float input);

#ifdef __cplusplus
}
#endif

#endif /* FILTER_H */