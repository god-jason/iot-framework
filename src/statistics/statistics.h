#ifndef STATISTICS_H
#define STATISTICS_H

/*
 * 在线统计（Welford 算法）：均值 / 方差 / 标准差 / 最小 / 最大。
 *
 * 特点：流式单遍更新，不需保存历史样本，内存占用固定，数值稳定性优于
 *       “平方和 - 均方” 的传统写法；复杂度 O(1)/样本。
 * 注意：方差为样本方差（除以 n-1）；样本数不足时相关接口返回 0。
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t count;         /* 已累计的样本数 n */
    float sum;              /* 样本总和（便于外部扩展使用） */
    float mean;             /* 当前均值 */
    float m2;      /* 偏差平方和 */
    float min;              /* 历史最小值 */
    float max;              /* 历史最大值 */
} statistics_t;

/* 初始化统计器（清零全部字段）。 */
void statistics_init(statistics_t *s);

/*
 * 送入一个样本并更新统计量。
 * 参数：s 统计器（非空）；value 新样本值。
 */
void statistics_update(statistics_t *s, float value);

/* 返回均值；s 为空或 count==0 时返回 0。 */
float statistics_mean(const statistics_t *s);

/* 返回样本方差 M2/(n-1)；n<2 时返回 0。 */
float statistics_variance(const statistics_t *s);

/* 返回标准差 sqrt(variance)；n<2 时返回 0。 */
float statistics_stddev(const statistics_t *s);

/* 返回最小值；s 为空或 count==0 时返回 0。 */
float statistics_min(const statistics_t *s);

/* 返回最大值；s 为空或 count==0 时返回 0。 */
float statistics_max(const statistics_t *s);

/* 复位统计器（等价于 statistics_init）。 */
void statistics_reset(statistics_t *s);

#ifdef __cplusplus
}
#endif

#endif /* STATISTICS_H */
