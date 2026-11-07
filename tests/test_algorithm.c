/* 算法模块测试：PID、数字滤波、统计、插值。 */
#include "test_framework.h"
#include "pid.h"
#include "filter.h"
#include "statistics.h"
#include "interpolation.h"

static void test_pid(void)
{
    pid_t pid;
    pid_incremental_t inc;

    tf_begin("algorithm/pid 位置式比例");
    TF_CHECK_INT(pid_init(&pid, 2.0f, 0.0f, 0.0f), 0);
    TF_CHECK_NEAR(pid_update(&pid, 10.0f, 0.0f, 1.0f), 20.0f, 1e-4);
    TF_CHECK_NEAR(pid_update(&pid, 10.0f, 0.0f, 1.0f), 20.0f, 1e-4);
    tf_end();

    tf_begin("algorithm/pid 积分作用");
    TF_CHECK_INT(pid_init(&pid, 0.0f, 1.0f, 0.0f), 0);
    TF_CHECK_NEAR(pid_update(&pid, 10.0f, 0.0f, 0.5f), 0.0f, 1e-4);
    TF_CHECK_NEAR(pid_update(&pid, 10.0f, 0.0f, 0.5f), 5.0f, 1e-4);
    tf_end();

    tf_begin("algorithm/pid 输出限幅与复位");
    TF_CHECK_INT(pid_init(&pid, 100.0f, 0.0f, 0.0f), 0);
    pid_set_limits(&pid, -5.0f, 5.0f);
    TF_CHECK_NEAR(pid_update(&pid, 100.0f, 0.0f, 1.0f), 5.0f, 1e-4);
    pid_reset(&pid);
    TF_CHECK_NEAR(pid.output, 0.0f, 1e-4);
    TF_CHECK(!pid.initialized);
    tf_end();

    tf_begin("algorithm/pid 增量式");
    TF_CHECK_INT(pid_incremental_init(&inc, 1.0f, 0.0f, 0.0f), 0);
    TF_CHECK_NEAR(pid_incremental_update(&inc, 10.0f, 0.0f), 0.0f, 1e-4);
    TF_CHECK_NEAR(pid_incremental_update(&inc, 20.0f, 0.0f), 10.0f, 1e-4);
    TF_CHECK_NEAR(pid_incremental_update(&inc, 20.0f, 0.0f), 10.0f, 1e-4);
    tf_end();

    tf_begin("algorithm/pid 参数保护");
    TF_CHECK_INT(pid_init(NULL, 1.0f, 0.0f, 0.0f), -1);
    TF_CHECK_NEAR(pid_update(NULL, 1.0f, 0.0f, 1.0f), 0.0f, 1e-4);
    tf_end();
}

static void test_filter(void)
{
    float mean_buf[3];
    float median_buf[5];
    float median_work[5];
    filter_mean_t mean;
    filter_median_t median;
    filter_ema_t ema;
    filter_lowpass_t lp;
    filter_highpass_t hp;
    filter_limit_t lim;
    filter_kalman_t kal;
    filter_hysteresis_t hys;

    tf_begin("algorithm/filter 滑动平均");
    TF_CHECK_INT(filter_mean_init(&mean, mean_buf, 3), 0);
    TF_CHECK_NEAR(filter_mean_update(&mean, 1.0f), 1.0f, 1e-4);
    TF_CHECK_NEAR(filter_mean_update(&mean, 2.0f), 1.5f, 1e-4);
    TF_CHECK_NEAR(filter_mean_update(&mean, 3.0f), 2.0f, 1e-4);
    TF_CHECK_NEAR(filter_mean_update(&mean, 4.0f), 3.0f, 1e-4);
    tf_end();

    tf_begin("algorithm/filter 中值");
    TF_CHECK_INT(filter_median_init(&median, median_buf, median_work, 5), 0);
    TF_CHECK_NEAR(filter_median_update(&median, 5.0f), 5.0f, 1e-4);
    /* 偶数个样本取上中位数：{1,5} -> 5 */
    TF_CHECK_NEAR(filter_median_update(&median, 1.0f), 5.0f, 1e-4);
    /* 奇数个样本 {1,3,5} -> 3 */
    TF_CHECK_NEAR(filter_median_update(&median, 3.0f), 3.0f, 1e-4);
    /* {1,3,5,7} -> 5 */
    TF_CHECK_NEAR(filter_median_update(&median, 7.0f), 5.0f, 1e-4);
    /* 窗口满 {1,3,5,7,9} -> 5 */
    TF_CHECK_NEAR(filter_median_update(&median, 9.0f), 5.0f, 1e-4);
    tf_end();

    tf_begin("algorithm/filter EMA");
    filter_ema_init(&ema, 0.5f);
    TF_CHECK_NEAR(filter_ema_update(&ema, 10.0f), 10.0f, 1e-4);
    TF_CHECK_NEAR(filter_ema_update(&ema, 20.0f), 15.0f, 1e-4);
    TF_CHECK_NEAR(filter_ema_update(&ema, 20.0f), 17.5f, 1e-4);
    tf_end();

    tf_begin("algorithm/filter 低通/高通");
    TF_CHECK_INT(filter_lowpass_init(&lp, 10.0f, 100.0f), 0);
    TF_CHECK_NEAR(filter_lowpass_update(&lp, 100.0f), 100.0f, 1e-4);
    TF_CHECK(filter_lowpass_update(&lp, 0.0f) < 100.0f);

    TF_CHECK_INT(filter_highpass_init(&hp, 10.0f, 100.0f), 0);
    TF_CHECK_NEAR(filter_highpass_update(&hp, 100.0f), 0.0f, 1e-4);
    tf_end();

    tf_begin("algorithm/filter 限幅");
    filter_limit_init(&lim, 1.0f);
    TF_CHECK_NEAR(filter_limit_update(&lim, 5.0f), 5.0f, 1e-4);
    TF_CHECK_NEAR(filter_limit_update(&lim, 10.0f), 6.0f, 1e-4);
    TF_CHECK_NEAR(filter_limit_update(&lim, 3.0f), 5.0f, 1e-4);
    tf_end();

    tf_begin("algorithm/filter 卡尔曼");
    filter_kalman_init(&kal, 0.01f, 1.0f, 0.0f);
    TF_CHECK_NEAR(filter_kalman_update(&kal, 10.0f), 5.0249f, 0.01);
    tf_end();

    tf_begin("algorithm/filter 滞回");
    filter_hysteresis_init(&hys, 30.0f, 70.0f);
    TF_CHECK(filter_hysteresis_update(&hys, 80.0f) == true);
    TF_CHECK(filter_hysteresis_update(&hys, 50.0f) == true);
    TF_CHECK(filter_hysteresis_update(&hys, 20.0f) == false);
    TF_CHECK(filter_hysteresis_update(&hys, 50.0f) == false);
    TF_CHECK(filter_hysteresis_update(&hys, 80.0f) == true);
    tf_end();
}

static void test_statistics(void)
{
    statistics_t s;
    static const float values[] = { 2, 4, 4, 4, 5, 5, 7, 9 };
    uint32_t i;

    tf_begin("algorithm/statistics Welford");
    statistics_init(&s);
    for (i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
        statistics_update(&s, values[i]);
    }
    TF_CHECK_INT(s.count, 8);
    TF_CHECK_NEAR(statistics_mean(&s), 5.0f, 1e-4);
    TF_CHECK_NEAR(statistics_variance(&s), 4.5714f, 1e-3);
    TF_CHECK_NEAR(statistics_stddev(&s), 2.1381f, 1e-3);
    TF_CHECK_NEAR(statistics_min(&s), 2.0f, 1e-4);
    TF_CHECK_NEAR(statistics_max(&s), 9.0f, 1e-4);
    tf_end();

    tf_begin("algorithm/statistics 空与重置");
    statistics_init(&s);
    TF_CHECK_NEAR(statistics_mean(&s), 0.0f, 1e-4);
    TF_CHECK_NEAR(statistics_variance(&s), 0.0f, 1e-4);
    statistics_update(&s, 3.0f);
    statistics_reset(&s);
    TF_CHECK_INT(s.count, 0);
    tf_end();
}

static void test_interp(void)
{
    static const float xs[] = { 0.0f, 10.0f, 20.0f };
    static const float ys[] = { 0.0f, 100.0f, 200.0f };
    float out = 0.0f;

    tf_begin("algorithm/interp 线性");
    TF_CHECK_NEAR(interpolation_linear(5.0f, 0.0f, 0.0f, 10.0f, 100.0f), 50.0f, 1e-4);
    TF_CHECK_NEAR(interpolation_linear(7.0f, 5.0f, 1.0f, 5.0f, 9.0f), 1.0f, 1e-4);
    tf_end();

    tf_begin("algorithm/interp 查表");
    TF_CHECK_INT(interpolation_lookup(5.0f, xs, ys, 3, &out), 0);
    TF_CHECK_NEAR(out, 50.0f, 1e-4);
    TF_CHECK_INT(interpolation_lookup(-5.0f, xs, ys, 3, &out), 0);
    TF_CHECK_NEAR(out, 0.0f, 1e-4);
    TF_CHECK_INT(interpolation_lookup(25.0f, xs, ys, 3, &out), 0);
    TF_CHECK_NEAR(out, 200.0f, 1e-4);
    TF_CHECK_INT(interpolation_lookup(10.0f, xs, ys, 3, &out), 0);
    TF_CHECK_NEAR(out, 100.0f, 1e-4);
    TF_CHECK_INT(interpolation_lookup(5.0f, xs, ys, 1, &out), -1);
    tf_end();

    tf_begin("algorithm/interp Catmull-Rom");
    {
        static const float pts[] = { 0.0f, 1.0f, 2.0f, 3.0f };
        TF_CHECK_NEAR(interpolation_cubic(0.0f, pts, 4), 0.0f, 1e-4);
        TF_CHECK_NEAR(interpolation_cubic(1.0f, pts, 4), 1.0f, 1e-4);
        TF_CHECK_NEAR(interpolation_cubic(2.0f, pts, 4), 2.0f, 1e-4);
        TF_CHECK_NEAR(interpolation_cubic(5.0f, pts, 2), 0.0f, 1e-4);
    }
    tf_end();
}

void test_algorithm(void)
{
    test_pid();
    test_filter();
    test_statistics();
    test_interp();
}
