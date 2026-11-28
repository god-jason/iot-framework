/*
 * statistics_test.c - 在线统计测试（对应 statistics.c）
 *
 * 覆盖：均值/样本方差/标准差/极值、单样本与空统计、
 *       复位，以及与解析值的对比。
 */

#include "test.h"
#include "statistics.h"

void ut_statistics(void)
{
    statistics_t s;

    tf_begin("statistics/基本统计量");

    statistics_init(&s);
    TF_CHECK_INT(s.count, 0);
    TF_CHECK_NEAR(statistics_mean(&s), 0.0f, 1e-6);
    TF_CHECK_NEAR(statistics_variance(&s), 0.0f, 1e-6);
    TF_CHECK_NEAR(statistics_stddev(&s), 0.0f, 1e-6);
    TF_CHECK_NEAR(statistics_min(&s), 0.0f, 1e-6);
    TF_CHECK_NEAR(statistics_max(&s), 0.0f, 1e-6);

    statistics_update(&s, 2.0f);
    TF_CHECK_INT(s.count, 1);
    TF_CHECK_NEAR(statistics_mean(&s), 2.0f, 1e-6);
    TF_CHECK_NEAR(statistics_variance(&s), 0.0f, 1e-6);   /* n<2 */

    statistics_update(&s, 4.0f);
    statistics_update(&s, 6.0f);
    TF_CHECK_INT(s.count, 3);
    TF_CHECK_NEAR(statistics_mean(&s), 4.0f, 1e-6);
    /* 样本方差：((2-4)^2+(4-4)^2+(6-4)^2)/2 = 4 */
    TF_CHECK_NEAR(statistics_variance(&s), 4.0f, 1e-5);
    TF_CHECK_NEAR(statistics_stddev(&s), 2.0f, 1e-5);
    TF_CHECK_NEAR(statistics_min(&s), 2.0f, 1e-6);
    TF_CHECK_NEAR(statistics_max(&s), 6.0f, 1e-6);

    tf_end();

    tf_begin("statistics/极值与复位");

    statistics_init(&s);
    statistics_update(&s, -1.5f);
    statistics_update(&s, 3.5f);
    statistics_update(&s, 0.0f);
    TF_CHECK_NEAR(statistics_min(&s), -1.5f, 1e-6);
    TF_CHECK_NEAR(statistics_max(&s), 3.5f, 1e-6);
    TF_CHECK_NEAR(statistics_mean(&s), 2.0f / 3.0f, 1e-5);

    statistics_reset(&s);
    TF_CHECK_INT(s.count, 0);
    TF_CHECK_NEAR(statistics_mean(&s), 0.0f, 1e-6);

    /* NULL 安全 */
    statistics_update(NULL, 1.0f);
    statistics_reset(NULL);
    TF_CHECK_NEAR(statistics_mean(NULL), 0.0f, 1e-6);
    TF_CHECK_NEAR(statistics_variance(NULL), 0.0f, 1e-6);
    TF_CHECK_NEAR(statistics_min(NULL), 0.0f, 1e-6);
    TF_CHECK_NEAR(statistics_max(NULL), 0.0f, 1e-6);

    tf_end();
}
