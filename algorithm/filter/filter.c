#include "filter.h"

#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

/* ---- 均值 ---- */
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

void filter_mean_reset(filter_mean_t *f)
{
    if (f == NULL) {
        return;
    }
    f->index = 0;
    f->count = 0;
    f->sum = 0.0f;
}

float filter_mean_update(filter_mean_t *f, float input)
{
    if (f == NULL) {
        return input;
    }
    if (f->count < f->size) {
        f->buffer[f->count] = input;
        f->sum += input;
        f->count++;
    } else {
        f->sum += input - f->buffer[f->index];
        f->buffer[f->index] = input;
        f->index = (uint16_t)((f->index + 1) % f->size);
    }
    return f->sum / (float)f->count;
}

/* ---- 中值 ---- */
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

float filter_median_update(filter_median_t *f, float input)
{
    uint16_t i, j;
    uint16_t n;

    if (f == NULL) {
        return input;
    }
    /* 环形窗口 */
    f->buffer[f->index] = input;
    f->index = (uint16_t)((f->index + 1) % f->size);
    if (f->count < f->size) {
        f->count++;
    }
    n = f->count;

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
void filter_ema_init(filter_ema_t *f, float alpha)
{
    if (f == NULL) {
        return;
    }
    f->alpha = alpha;
    f->output = 0.0f;
    f->initialized = false;
}

void filter_ema_reset(filter_ema_t *f)
{
    if (f == NULL) {
        return;
    }
    f->output = 0.0f;
    f->initialized = false;
}

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

float filter_lowpass_update(filter_lowpass_t *f, float input)
{
    if (f == NULL) {
        return input;
    }
    if (!f->initialized) {
        f->output = input;
        f->initialized = true;
    } else {
        f->output = f->output + f->alpha * (input - f->output);
    }
    return f->output;
}

/* ---- 一阶高通 ---- */
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

float filter_highpass_update(filter_highpass_t *f, float input)
{
    float output;

    if (f == NULL) {
        return input;
    }
    if (!f->initialized) {
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
void filter_limit_init(filter_limit_t *f, float max_change)
{
    if (f == NULL) {
        return;
    }
    f->max_change = max_change;
    f->last_value = 0.0f;
    f->initialized = false;
}

float filter_limit_update(filter_limit_t *f, float input)
{
    float delta;

    if (f == NULL) {
        return input;
    }
    if (!f->initialized) {
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