#ifndef FILTER_H
#define FILTER_H

/*
 * 常用数字滤波器：均值 / 滑动平均 / 中值 / EMA / 一阶低通 / 一阶高通 / 限幅 / 卡尔曼 / 滞回。
 * 算法库独立于 Core 与 HAL；缓冲区由调用方提供，无动态内存。
 *
 * 使用方式：先调用对应 *_init 初始化，再对每个采样点调用 *_update；
 * 需要重新开始滤波时调用 *_reset（部分滤波器提供）。
 * 所有 *_update 均做空指针保护：传入 NULL 时原样返回输入。
 */

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 均值 / 滑动平均 ---- */
typedef struct {
    float *buffer;          /* 外部样本缓冲区，长度 = size */
    uint16_t size;          /* 窗口长度（样本个数） */
    uint16_t index;         /* 环形写指针，饱和后指向最旧样本 */
    uint16_t count;         /* 当前窗口内有效样本数（<= size） */
    float sum;              /* 窗口内样本求和，用于增量更新均值 */
} filter_mean_t;

/*
 * 初始化滑动均值滤波器。
 * 参数：f 滤波器对象；buffer/size 调用方提供的缓冲区及窗口长度。
 * 返回：0 成功；-1 参数非法。
 */
int filter_mean_init(filter_mean_t *f, float *buffer, uint16_t size);

/* 更新一格并返回当前窗口均值；f 为空时返回输入。 */
float filter_mean_update(filter_mean_t *f, float input);

/* 清空窗口统计量（不清除缓冲区内容）。 */
void filter_mean_reset(filter_mean_t *f);

/* ---- 中值 ---- */
typedef struct {
    float *buffer;          /* 外部环形样本缓冲区，长度 = size */
    float *work;            /* 排序工作区，长度 >= size */
    uint16_t size;          /* 窗口长度 */
    uint16_t index;         /* 环形写指针 */
    uint16_t count;         /* 当前有效样本数 */
} filter_median_t;

/*
 * 初始化中值滤波器。
 * 参数：f 滤波器对象；buffer/size 样本环形缓冲；work 排序工作区。
 * 返回：0 成功；-1 参数非法。
 */
int filter_median_init(filter_median_t *f, float *buffer,
                           float *work, uint16_t size);

/* 更新一格并返回窗口中位数；f 为空时返回输入。 */
float filter_median_update(filter_median_t *f, float input);

/* ---- EMA ---- */
typedef struct {
    float alpha;            /* 平滑系数 α∈(0,1]，越大越跟随输入 */
    float output;           /* 上次滤波输出 */
    bool initialized;       /* 是否已用首个样本初始化 */
} filter_ema_t;

/*
 * 初始化 EMA 滤波器。
 * 参数：f 滤波器；alpha 平滑系数。
 */
void filter_ema_init(filter_ema_t *f, float alpha);

/* 更新一格并返回 EMA 输出；f 为空时返回输入。 */
float filter_ema_update(filter_ema_t *f, float input);

/* 复位输出状态。 */
void filter_ema_reset(filter_ema_t *f);

/* ---- 一阶低通 ---- */
typedef struct {
    float alpha;            /* 滤波系数 α = dt/(RC+dt) */
    float output;           /* 上次滤波输出 */
    bool initialized;       /* 是否已初始化 */
} filter_lowpass_t;

/*
 * 初始化一阶低通滤波器。
 * 参数：f 滤波器；cutoff 截止频率(Hz)；sample_rate 采样率(Hz)。
 * 返回：0 成功；-1 参数非法。
 */
int filter_lowpass_init(filter_lowpass_t *f, float cutoff, float sample_rate);

/* 更新一格并返回低通输出；f 为空时返回输入。 */
float filter_lowpass_update(filter_lowpass_t *f, float input);

/* ---- 一阶高通 ---- */
typedef struct {
    float alpha;            /* 滤波系数 α = RC/(RC+dt) */
    float prev_input;       /* x[k-1] */
    float prev_output;      /* y[k-1] */
    bool initialized;       /* 是否已初始化 */
} filter_highpass_t;

/*
 * 初始化一阶高通滤波器。
 * 参数：f 滤波器；cutoff 截止频率(Hz)；sample_rate 采样率(Hz)。
 * 返回：0 成功；-1 参数非法。
 */
int filter_highpass_init(filter_highpass_t *f, float cutoff, float sample_rate);

/* 更新一格并返回高通输出；首次调用返回 0。 */
float filter_highpass_update(filter_highpass_t *f, float input);

/* ---- 限幅 ---- */
typedef struct {
    float max_change;       /* 单步允许的最大变化量 */
    float last_value;       /* 上次输出值 */
    bool initialized;       /* 是否已初始化 */
} filter_limit_t;

/*
 * 初始化变化率限幅器。
 * 参数：f 限幅器；max_change 单步最大变化量。
 */
void filter_limit_init(filter_limit_t *f, float max_change);

/* 更新一格并返回限幅后的输出；f 为空时返回输入。 */
float filter_limit_update(filter_limit_t *f, float input);

/* ---- 一维卡尔曼 ---- */
typedef struct {
    float q;                /* 过程噪声协方差 */
    float r;                /* 测量噪声协方差 */
    float x;                /* 状态估计值 */
    float p;                /* 估计误差协方差 */
    float k;                /* 卡尔曼增益 */
} filter_kalman_t;

/*
 * 初始化一维卡尔曼滤波器。
 * 参数：f 滤波器；q 过程噪声；r 测量噪声；initial_value 状态初值。
 */
void filter_kalman_init(filter_kalman_t *f, float q, float r, float initial_value);

/* 更新一格并返回状态估计；f 为空时返回测量值。 */
float filter_kalman_update(filter_kalman_t *f, float measurement);

/* ---- 滞回（开关量去抖） ---- */
typedef struct {
    float low;              /* 复位阈值 */
    float high;             /* 置位阈值 */
    bool state;             /* 当前开关状态 */
    bool initialized;       /* 是否已初始化 */
} filter_hysteresis_t;

/*
 * 初始化滞回比较器。
 * 参数：f 滞回器；low 复位阈值；high 置位阈值（应满足 low < high）。
 */
void filter_hysteresis_init(filter_hysteresis_t *f, float low, float high);

/* 更新一格并返回当前开关状态；f 为空时返回 false。 */
bool filter_hysteresis_update(filter_hysteresis_t *f, float input);

#ifdef __cplusplus
}
#endif

#endif /* FILTER_H */
