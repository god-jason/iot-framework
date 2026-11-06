#include "iot_statistics.h"
#include <math.h>

void iot_statistics_init(iot_statistics_t *s)
{
    if (s == NULL) {
        return;
    }
    s->count = 0;
    s->sum = 0.0f;
    s->mean = 0.0f;
    s->m2 = 0.0f;
    s->min = 0.0f;
    s->max = 0.0f;
}

void iot_statistics_update(iot_statistics_t *s, float value)
{
    float delta, delta2;

    if (s == NULL) {
        return;
    }
    s->count++;
    s->sum += value;

    if (s->count == 1) {
        s->min = value;
        s->max = value;
    } else {
        if (value < s->min) s->min = value;
        if (value > s->max) s->max = value;
    }

    /* Welford 在线方差 */
    delta = value - s->mean;
    s->mean += delta / (float)s->count;
    delta2 = value - s->mean;
    s->m2 += delta * delta2;
}

float iot_statistics_mean(const iot_statistics_t *s)
{
    return (s == NULL || s->count == 0) ? 0.0f : s->mean;
}

float iot_statistics_variance(const iot_statistics_t *s)
{
    /* 样本方差（n-1） */
    if (s == NULL || s->count < 2) {
        return 0.0f;
    }
    return s->m2 / (float)(s->count - 1);
}

float iot_statistics_stddev(const iot_statistics_t *s)
{
    return sqrtf(iot_statistics_variance(s));
}

float iot_statistics_min(const iot_statistics_t *s)
{
    return (s == NULL || s->count == 0) ? 0.0f : s->min;
}

float iot_statistics_max(const iot_statistics_t *s)
{
    return (s == NULL || s->count == 0) ? 0.0f : s->max;
}

void iot_statistics_reset(iot_statistics_t *s)
{
    iot_statistics_init(s);
}