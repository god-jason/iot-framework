/* algorithm/statistics 单元测试：均值/样本方差/标准差/极值与空集行为。 */
#include "test.h"
#include "statistics.h"

static const float s_values[] = { 2.0f, 4.0f, 4.0f, 4.0f, 5.0f, 5.0f, 7.0f, 9.0f };

void ut_statistics(void)
{
    statistics_t s;
    uint32_t i;

    tf_begin("algorithm/statistics 空数据集");
    memset(&s, 0xff, sizeof(s));
    statistics_init(&s);
    TF_CHECK_INT(s.count, 0);
    TF_CHECK_NEAR(statistics_mean(&s), 0.0f, 1e-6);
    TF_CHECK_NEAR(statistics_variance(&s), 0.0f, 1e-6);
    TF_CHECK_NEAR(statistics_stddev(&s), 0.0f, 1e-6);
    TF_CHECK_NEAR(statistics_min(&s), 0.0f, 1e-6);
    TF_CHECK_NEAR(statistics_max(&s), 0.0f, 1e-6);
    tf_end();

    tf_begin("algorithm/statistics 单元素");
    statistics_init(&s);
    statistics_update(&s, 7.0f);
    TF_CHECK_INT(s.count, 1);
    TF_CHECK_NEAR(statistics_mean(&s), 7.0f, 1e-6);
    TF_CHECK_NEAR(statistics_variance(&s), 0.0f, 1e-6);
    TF_CHECK_NEAR(statistics_stddev(&s), 0.0f, 1e-6);
    TF_CHECK_NEAR(statistics_min(&s), 7.0f, 1e-6);
    TF_CHECK_NEAR(statistics_max(&s), 7.0f, 1e-6);
    tf_end();

    tf_begin("algorithm/statistics Welford 统计量");
    statistics_init(&s);
    for (i = 0; i < sizeof(s_values) / sizeof(s_values[0]); i++) {
        statistics_update(&s, s_values[i]);
    }
    TF_CHECK_INT(s.count, 8);
    TF_CHECK_NEAR(s.sum, 40.0f, 1e-4);
    TF_CHECK_NEAR(statistics_mean(&s), 5.0f, 1e-4);
    TF_CHECK_NEAR(statistics_variance(&s), 4.5714f, 1e-3);
    TF_CHECK_NEAR(statistics_stddev(&s), 2.1381f, 1e-3);
    TF_CHECK_NEAR(statistics_min(&s), 2.0f, 1e-4);
    TF_CHECK_NEAR(statistics_max(&s), 9.0f, 1e-4);
    tf_end();

    tf_begin("algorithm/statistics 两元素样本方差");
    statistics_init(&s);
    statistics_update(&s, 1.0f);
    statistics_update(&s, 3.0f);
    TF_CHECK_INT(s.count, 2);
    TF_CHECK_NEAR(statistics_mean(&s), 2.0f, 1e-6);
    TF_CHECK_NEAR(statistics_variance(&s), 2.0f, 1e-6);
    TF_CHECK_NEAR(statistics_stddev(&s), 1.4142f, 1e-3);
    tf_end();

    tf_begin("algorithm/statistics 负值与重复值");
    statistics_init(&s);
    statistics_update(&s, -5.0f);
    statistics_update(&s, -5.0f);
    statistics_update(&s, 0.0f);
    TF_CHECK_INT(s.count, 3);
    TF_CHECK_NEAR(statistics_mean(&s), -10.0f / 3.0f, 1e-4);
    TF_CHECK_NEAR(statistics_min(&s), -5.0f, 1e-6);
    TF_CHECK_NEAR(statistics_max(&s), 0.0f, 1e-6);
    TF_CHECK(statistics_variance(&s) > 0.0f);
    tf_end();

    tf_begin("algorithm/statistics 重置");
    statistics_init(&s);
    statistics_update(&s, 3.0f);
    statistics_update(&s, 9.0f);
    statistics_reset(&s);
    TF_CHECK_INT(s.count, 0);
    TF_CHECK_NEAR(statistics_mean(&s), 0.0f, 1e-6);
    TF_CHECK_NEAR(statistics_min(&s), 0.0f, 1e-6);
    TF_CHECK_NEAR(statistics_max(&s), 0.0f, 1e-6);
    statistics_update(&s, -2.0f);
    TF_CHECK_INT(s.count, 1);
    TF_CHECK_NEAR(statistics_min(&s), -2.0f, 1e-6);
    TF_CHECK_NEAR(statistics_max(&s), -2.0f, 1e-6);
    tf_end();

    tf_begin("algorithm/statistics 空指针保护");
    statistics_init(NULL);
    statistics_update(NULL, 1.0f);
    statistics_reset(NULL);
    TF_CHECK_NEAR(statistics_mean(NULL), 0.0f, 1e-6);
    TF_CHECK_NEAR(statistics_variance(NULL), 0.0f, 1e-6);
    TF_CHECK_NEAR(statistics_stddev(NULL), 0.0f, 1e-6);
    TF_CHECK_NEAR(statistics_min(NULL), 0.0f, 1e-6);
    TF_CHECK_NEAR(statistics_max(NULL), 0.0f, 1e-6);
    tf_end();
}
