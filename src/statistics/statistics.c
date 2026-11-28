/*
 * 在线统计实现（Welford 增量算法）。
 *
 * 无需保存历史样本，每来一个观测值即可增量更新均值、方差、极值，空间 O(1)。
 *
 * 公式（n 为已处理样本数，mean 为当前均值，M2 为偏差平方和）：
 *   delta  = x - mean
 *   mean  += delta / n
 *   delta2 = x - mean            （更新后的均值）
 *   M2    += delta * delta2
 *   样本方差 s² = M2 / (n-1)     （n >= 2）
 *   标准差   s  = sqrt(s²)
 *
 * 数据流：调用方先 statistics_init，再逐个 statistics_update(value)，
 *         之后可随时读取 mean/variance/stddev/min/max。
 * 边界：n == 0 时均值与极值返回 0；n < 2 时方差/标准差返回 0。
 */
#include "statistics.h"
#include <math.h>

/* 初始化统计器：计数、求和、均值、M2、极值全部清零。 */
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

/*
 * 送入一个样本并增量更新全部统计量。
 * 参数：s 统计器；value 新观测值。
 * 说明：首次样本同时初始化 min/max；均值与 M2 按 Welford 算法更新，
 *       相比 sum/count 方式数值稳定性更好（避免大数相减的抵消误差）。
 */
void statistics_update(statistics_t *s, float value)
{
    float delta, delta2;

    if (s == NULL) {
        return;
    }
    s->count++;
    s->sum += value;

    /* 首个样本初始化极值，之后逐点比较更新 */
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

/* 返回当前均值；s 为空或无样本时返回 0。 */
float statistics_mean(const statistics_t *s)
{
    return (s == NULL || s->count == 0) ? 0.0f : s->mean;
}

/* 返回样本方差（除以 n-1）；样本数 < 2 时返回 0。 */
float statistics_variance(const statistics_t *s)
{
    /* 样本方差（n-1） */
    if (s == NULL || s->count < 2) {
        return 0.0f;
    }
    return s->m2 / (float)(s->count - 1);
}

/* 返回样本标准差（方差的平方根）；样本数 < 2 时返回 0。 */
float statistics_stddev(const statistics_t *s)
{
    return sqrtf(statistics_variance(s));
}

/* 返回历史最小值；s 为空或无样本时返回 0。 */
float statistics_min(const statistics_t *s)
{
    return (s == NULL || s->count == 0) ? 0.0f : s->min;
}

/* 返回历史最大值；s 为空或无样本时返回 0。 */
float statistics_max(const statistics_t *s)
{
    return (s == NULL || s->count == 0) ? 0.0f : s->max;
}

/* 复位统计器，等价于 statistics_init。 */
void statistics_reset(statistics_t *s)
{
    statistics_init(s);
}
