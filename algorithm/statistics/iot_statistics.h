#ifndef IOT_STATISTICS_H
#define IOT_STATISTICS_H

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
} iot_statistics_t;

void iot_statistics_init(iot_statistics_t *s);
void iot_statistics_update(iot_statistics_t *s, float value);

float iot_statistics_mean(const iot_statistics_t *s);
float iot_statistics_variance(const iot_statistics_t *s);
float iot_statistics_stddev(const iot_statistics_t *s);
float iot_statistics_min(const iot_statistics_t *s);
float iot_statistics_max(const iot_statistics_t *s);

void iot_statistics_reset(iot_statistics_t *s);

#ifdef __cplusplus
}
#endif

#endif /* IOT_STATISTICS_H */