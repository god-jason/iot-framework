#include "statistics.h"
#include <math.h>

void statistics_init(statistics_t *s)
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

void statistics_update(statistics_t *s, float value)
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

float statistics_mean(const statistics_t *s)
{
    return (s == NULL || s->count == 0) ? 0.0f : s->mean;
}

float statistics_variance(const statistics_t *s)
{
    /* 样本方差（n-1） */
    if (s == NULL || s->count < 2) {
        return 0.0f;
    }
    return s->m2 / (float)(s->count - 1);
}

float statistics_stddev(const statistics_t *s)
{
    return sqrtf(statistics_variance(s));
}

float statistics_min(const statistics_t *s)
{
    return (s == NULL || s->count == 0) ? 0.0f : s->min;
}

float statistics_max(const statistics_t *s)
{
    return (s == NULL || s->count == 0) ? 0.0f : s->max;
}

void statistics_reset(statistics_t *s)
{
    statistics_init(s);
}