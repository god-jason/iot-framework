/*
 * 常用数字滤波器实现。
 *
 * 所有滤波器均为单输入单输出、逐采样点更新（每来一个样本调用一次 *_update）。
 * 平均值/中值滤波器使用调用方提供的缓冲区，不做动态内存分配。
 * 约定：*_update 在 f == NULL 时直接返回输入；各滤波器首次输出均以首个
 *       样本初始化，避免从 0 缓慢爬升。调用 *_reset 可清除历史状态。
 *
 * 主要公式：
 *   滑动均值  y = (Σ窗口内样本) / N            —— 线性相位，抑制随机噪声
 *   中值      y = sort(窗口)[N/2]              —— 抑制脉冲型野值
 *   EMA       y[k] = α*x[k] + (1-α)*y[k-1]     —— 指数加权，α∈(0,1]
 *   一阶低通  RC = 1/(2πfc)，α = dt/(RC+dt)，y += α*(x-y)
 *   一阶高通  α = RC/(RC+dt)，y[k] = α*(y[k-1] + x[k] - x[k-1])
 *   限幅      单步变化量被限制在 ±max_change 内
 *   卡尔曼    一维标量：p += q; k = p/(p+r); x += k*(z-x); p = (1-k)*p
 *   滞回      输入 >= high 置位，<= low 复位，中间保持原状态
 */
#include "filter.h"

#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

/* ---- 均值 ---- */

/*
 * 初始化滑动平均值滤波器。
 * 参数：f 滤波器对象；buffer 长度为 size 的外部缓冲区；size 窗口长度（>0）。
 * 返回：0 成功；-1 参数非法。
 */
int filter_mean_init(filter_mean_t *f, float *buffer, uint16_t size)
{
    if (f == NULL || buffer == NULL || size == 0) {
        return -1;
    }
    f->buffer = buffer;
    f->size = size;
    filter_mean_reset(f);
    return 0;
}

/* 清空窗口（count/index/sum 归零），缓冲区内容不擦除。 */
void filter_mean_reset(filter_mean_t *f)
{
    if (f == NULL) {
        return;
    }
    f->index = 0;
    f->count = 0;
    f->sum = 0.0f;
}

/*
 * 滑动均值更新一格。
 * 参数：f 滤波器；input 新样本。
 * 返回：当前窗口内样本的算术平均值；f 为空时原样返回输入。
 * 说明：窗口未满时直接追加；已满时用新样本替换最旧样本，
 *       sum 采用增量更新（加上新值、减去旧值），避免整窗重复求和。
 */
float filter_mean_update(filter_mean_t *f, float input)
{
    if (f == NULL) {
        return input;
    }
    if (f->count < f->size) {
        /* 填充阶段：线性追加，count 同时是窗口内有效样本数 */
        f->buffer[f->count] = input;
        f->sum += input;
        f->count++;
    } else {
        /* 饱和阶段：index 指向最旧样本，替换并环形前移 */
        f->sum += input - f->buffer[f->index];
        f->buffer[f->index] = input;
        f->index = (uint16_t)((f->index + 1) % f->size);
    }
    return f->sum / (float)f->count;
}

/* ---- 中值 ---- */

/*
 * 初始化中值滤波器。
 * 参数：f 滤波器对象；buffer 环形样本缓冲（长度 size）；work 排序工作区
 *       （长度 size）；size 窗口长度（>0）。
 * 返回：0 成功；-1 参数非法。
 */
int filter_median_init(filter_median_t *f, float *buffer,
                           float *work, uint16_t size)
{
    if (f == NULL || buffer == NULL || work == NULL || size == 0) {
        return -1;
    }
    f->buffer = buffer;
    f->work = work;
    f->size = size;
    f->index = 0;
    f->count = 0;
    return 0;
}

/*
 * 中值滤波更新一格。
 * 参数：f 滤波器；input 新样本。
 * 返回：窗口内样本的中位数（偶数个时取上中位）；f 为空时原样返回输入。
 * 算法：样本写入环形窗口 -> 拷贝到工作区 -> 冒泡排序 -> 取 n/2 下标。
 * 复杂度 O(N²)，适用于小窗口（如 3/5/7 点）。
 */
float filter_median_update(filter_median_t *f, float input)
{
    uint16_t i, j;
    uint16_t n;

    if (f == NULL) {
        return input;
    }
    /* 环形窗口：index 指向下一个待写入位置 */
    f->buffer[f->index] = input;
    f->index = (uint16_t)((f->index + 1) % f->size);
    if (f->count < f->size) {
        f->count++;
    }
    n = f->count;

    /* 拷贝有效数据到工作区（不破坏原始环形顺序） */
    for (i = 0; i < n; i++) {
        f->work[i] = f->buffer[i];
    }
    /* 冒泡排序（窗口较小） */
    for (i = 0; i + 1 < n; i++) {
        for (j = 0; j + 1 < n - i; j++) {
            if (f->work[j] > f->work[j + 1]) {
                float t = f->work[j];
                f->work[j] = f->work[j + 1];
                f->work[j + 1] = t;
            }
        }
    }
    return f->work[n / 2];
}

/* ---- EMA ---- */

/*
 * 初始化指数加权平均（EMA）滤波器。
 * 参数：f 滤波器；alpha 平滑系数，取值 (0,1]。
 *       越大越贴近输入（响应快），越小越平滑（滞后大）。
 */
void filter_ema_init(filter_ema_t *f, float alpha)
{
    if (f == NULL) {
        return;
    }
    f->alpha = alpha;
    f->output = 0.0f;
    f->initialized = false;
}

/* 复位 EMA 输出，下次更新重新以首个样本初始化。 */
void filter_ema_reset(filter_ema_t *f)
{
    if (f == NULL) {
        return;
    }
    f->output = 0.0f;
    f->initialized = false;
}

/*
 * EMA 更新一格：y = α*x + (1-α)*y_prev。
 * 返回滤波输出；f 为空时原样返回输入；首次调用直接输出输入值。
 */
float filter_ema_update(filter_ema_t *f, float input)
{
    if (f == NULL) {
        return input;
    }
    if (!f->initialized) {
        f->output = input;
        f->initialized = true;
    } else {
        f->output = f->alpha * input + (1.0f - f->alpha) * f->output;
    }
    return f->output;
}

/* ---- 一阶低通 ---- */

/*
 * 初始化一阶 RC 低通滤波器。
 * 参数：f 滤波器；cutoff 截止频率（Hz，>0）；sample_rate 采样率（Hz，>0）。
 * 返回：0 成功；-1 参数非法。
 * 说明：RC = 1/(2πfc)，α = dt/(RC+dt)，dt = 1/sample_rate。
 *       截止频率越高，α 越大，输出越贴近输入。
 */
int filter_lowpass_init(filter_lowpass_t *f, float cutoff, float sample_rate)
{
    float rc, dt;

    if (f == NULL || cutoff <= 0.0f || sample_rate <= 0.0f) {
        return -1;
    }
    rc = 1.0f / (2.0f * (float)M_PI * cutoff);
    dt = 1.0f / sample_rate;
    f->alpha = dt / (rc + dt);
    f->output = 0.0f;
    f->initialized = false;
    return 0;
}

/* 一阶低通更新一格：y += α*(x-y)；返回滤波输出，f 为空时返回输入。 */
float filter_lowpass_update(filter_lowpass_t *f, float input)
{
    if (f == NULL) {
        return input;
    }
    if (!f->initialized) {
        /* 首个样本直接作为初值，避免从 0 缓慢上升 */
        f->output = input;
        f->initialized = true;
    } else {
        f->output = f->output + f->alpha * (input - f->output);
    }
    return f->output;
}

/* ---- 一阶高通 ---- */

/*
 * 初始化一阶 RC 高通滤波器。
 * 参数：f 滤波器；cutoff 截止频率（Hz，>0）；sample_rate 采样率（Hz，>0）。
 * 返回：0 成功；-1 参数非法。
 * 说明：RC = 1/(2πfc)，α = RC/(RC+dt)；α 越接近 1，低频衰减越强。
 */
int filter_highpass_init(filter_highpass_t *f, float cutoff, float sample_rate)
{
    float rc, dt;

    if (f == NULL || cutoff <= 0.0f || sample_rate <= 0.0f) {
        return -1;
    }
    rc = 1.0f / (2.0f * (float)M_PI * cutoff);
    dt = 1.0f / sample_rate;
    f->alpha = rc / (rc + dt);
    f->prev_input = 0.0f;
    f->prev_output = 0.0f;
    f->initialized = false;
    return 0;
}

/*
 * 一阶高通更新一格：y[k] = α*(y[k-1] + x[k] - x[k-1])。
 * 返回滤波输出；f 为空时原样返回输入；首次调用仅记录初值并返回 0。
 */
float filter_highpass_update(filter_highpass_t *f, float input)
{
    float output;

    if (f == NULL) {
        return input;
    }
    if (!f->initialized) {
        /* 首个样本仅用于建立 x[k-1] 基准，输出 0（无历史变化量） */
        f->prev_input = input;
        f->prev_output = 0.0f;
        f->initialized = true;
        return 0.0f;
    }
    output = f->alpha * (f->prev_output + input - f->prev_input);
    f->prev_input = input;
    f->prev_output = output;
    return output;
}

/* ---- 限幅 ---- */

/*
 * 初始化变化率限幅器。
 * 参数：f 限幅器；max_change 每个采样周期允许的最大变化量（绝对值）。
 */
void filter_limit_init(filter_limit_t *f, float max_change)
{
    if (f == NULL) {
        return;
    }
    f->max_change = max_change;
    f->last_value = 0.0f;
    f->initialized = false;
}

/*
 * 变化率限幅更新一格。
 * 参数：f 限幅器；input 期望值。
 * 返回：以 last_value 为起点、单步变化不超过 ±max_change 的实际输出。
 * 说明：用于抑制设定值跳变或执行机构冲击，本质是一阶斜率限制。
 */
float filter_limit_update(filter_limit_t *f, float input)
{
    float delta;

    if (f == NULL) {
        return input;
    }
    if (!f->initialized) {
        /* 首次直接接受输入作为基准 */
        f->last_value = input;
        f->initialized = true;
        return input;
    }
    delta = input - f->last_value;
    if (delta > f->max_change) {
        delta = f->max_change;
    } else if (delta < -f->max_change) {
        delta = -f->max_change;
    }
    f->last_value += delta;
    return f->last_value;
}

/* ---- 卡尔曼 ---- */

/*
 * 初始化一维标量卡尔曼滤波器。
 * 参数：f 滤波器；q 过程噪声协方差（越大越信任测量）；r 测量噪声协方差
 *       （越大越信任模型）；initial_value 状态初值。
 * 说明：初始估计协方差 p 取 1.0。
 */
void filter_kalman_init(filter_kalman_t *f, float q, float r, float initial_value)
{
    if (f == NULL) {
        return;
    }
    f->q = q;
    f->r = r;
    f->x = initial_value;
    f->p = 1.0f;
    f->k = 0.0f;
}

/*
 * 一维卡尔曼更新一格（预测 + 校正）。
 * 参数：f 滤波器；measurement 观测值 z。
 * 返回：更新后的状态估计 x；f 为空时直接返回观测值。
 * 步骤：
 *   1) 预测协方差 p = p + q
 *   2) 计算卡尔曼增益 k = p / (p + r)
 *   3) 状态校正   x = x + k*(z - x)
 *   4) 协方差更新 p = (1 - k)*p
 */
float filter_kalman_update(filter_kalman_t *f, float measurement)
{
    if (f == NULL) {
        return measurement;
    }
    f->p = f->p + f->q;
    f->k = f->p / (f->p + f->r);
    f->x = f->x + f->k * (measurement - f->x);
    f->p = (1.0f - f->k) * f->p;
    return f->x;
}

/* ---- 滞回 ---- */

/*
 * 初始化滞回比较器（开关量去抖）。
 * 参数：f 滞回器；low 复位阈值；high 置位阈值。
 * 说明：要求 low < high，两阈值之间的区间为保持区。
 */
void filter_hysteresis_init(filter_hysteresis_t *f, float low, float high)
{
    if (f == NULL) {
        return;
    }
    f->low = low;
    f->high = high;
    f->state = false;
    f->initialized = false;
}

/*
 * 滞回比较更新一格。
 * 参数：f 滞回器；input 输入量。
 * 返回：当前开关状态（true=置位）。
 * 行为：input >= high 置位；input <= low 复位；处于两者之间保持原状态。
 *       首次调用以 input >= high 判定初始状态。
 */
bool filter_hysteresis_update(filter_hysteresis_t *f, float input)
{
    if (f == NULL) {
        return false;
    }
    if (!f->initialized) {
        f->state = (input >= f->high);
        f->initialized = true;
    } else {
        if (input >= f->high) {
            f->state = true;
        } else if (input <= f->low) {
            f->state = false;
        }
    }
    return f->state;
}
