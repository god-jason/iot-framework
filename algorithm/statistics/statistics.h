#ifndef STATISTICS_H
#define STATISTICS_H

/*
 * 在线统计（Welford 算法）：均值 / 方差 / 标准差 / 最小 / 最大。
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t count;
    float sum;
    float mean;
    float m2;      /* 偏差平方和 */
    float min;
    float max;
} statistics_t;

void statistics_init(statistics_t *s);
void statistics_update(statistics_t *s, float value);

float statistics_mean(const statistics_t *s);
float statistics_variance(const statistics_t *s);
float statistics_stddev(const statistics_t *s);
float statistics_min(const statistics_t *s);
float statistics_max(const statistics_t *s);

void statistics_reset(statistics_t *s);

#ifdef __cplusplus
}
#endif

#endif /* STATISTICS_H */