/* algorithm/filter 单元测试：均值/中值/EMA/低通/高通/限幅/卡尔曼/滞回。 */
#include "test_framework.h"
#include "filter.h"

#include <math.h>

void ut_filter(void)
{
    float mean_buf[3];
    float median_buf[3];
    float median_work[3];
    float median_buf1[1];
    float median_work1[1];
    filter_mean_t mean;
    filter_median_t median;
    filter_ema_t ema;
    filter_lowpass_t lp;
    filter_highpass_t hp;
    filter_limit_t lim;
    filter_kalman_t kal;
    filter_hysteresis_t hys;

    tf_begin("algorithm/filter 滑动平均与环形窗口");
    memset(mean_buf, 0, sizeof(mean_buf));
    TF_CHECK_INT(filter_mean_init(&mean, mean_buf, 3), 0);
    TF_CHECK_NEAR(filter_mean_update(&mean, 1.0f), 1.0f, 1e-4);
    TF_CHECK_NEAR(filter_mean_update(&mean, 2.0f), 1.5f, 1e-4);
    TF_CHECK_NEAR(filter_mean_update(&mean, 3.0f), 2.0f, 1e-4);
    TF_CHECK_NEAR(filter_mean_update(&mean, 4.0f), 3.0f, 1e-4);
    TF_CHECK_NEAR(filter_mean_update(&mean, 5.0f), 4.0f, 1e-4);
    filter_mean_reset(&mean);
    TF_CHECK_INT(mean.count, 0);
    TF_CHECK_NEAR(filter_mean_update(&mean, 10.0f), 10.0f, 1e-4);
    tf_end();

    tf_begin("algorithm/filter 滑动平均错误参数");
    TF_CHECK_INT(filter_mean_init(NULL, mean_buf, 3), -1);
    TF_CHECK_INT(filter_mean_init(&mean, NULL, 3), -1);
    TF_CHECK_INT(filter_mean_init(&mean, mean_buf, 0), -1);
    TF_CHECK_NEAR(filter_mean_update(NULL, 3.5f), 3.5f, 1e-4);
    filter_mean_reset(NULL);
    tf_end();

    tf_begin("algorithm/filter 中值滤波");
    memset(median_buf, 0, sizeof(median_buf));
    memset(median_work, 0, sizeof(median_work));
    TF_CHECK_INT(filter_median_init(&median, median_buf, median_work, 3), 0);
    TF_CHECK_NEAR(filter_median_update(&median, 1.0f), 1.0f, 1e-4);
    TF_CHECK_NEAR(filter_median_update(&median, 2.0f), 2.0f, 1e-4);
    TF_CHECK_NEAR(filter_median_update(&median, 3.0f), 2.0f, 1e-4);
    TF_CHECK_NEAR(filter_median_update(&median, 4.0f), 3.0f, 1e-4);
    TF_CHECK_NEAR(filter_median_update(&median, 5.0f), 4.0f, 1e-4);
    tf_end();

    tf_begin("algorithm/filter 中值滤波边界");
    TF_CHECK_INT(filter_median_init(NULL, median_buf, median_work, 3), -1);
    TF_CHECK_INT(filter_median_init(&median, NULL, median_work, 3), -1);
    TF_CHECK_INT(filter_median_init(&median, median_buf, NULL, 3), -1);
    TF_CHECK_INT(filter_median_init(&median, median_buf, median_work, 0), -1);
    TF_CHECK_NEAR(filter_median_update(NULL, 1.5f), 1.5f, 1e-4);

    TF_CHECK_INT(filter_median_init(&median, median_buf1, median_work1, 1), 0);
    TF_CHECK_NEAR(filter_median_update(&median, 7.0f), 7.0f, 1e-4);
    TF_CHECK_NEAR(filter_median_update(&median, 3.0f), 3.0f, 1e-4);
    tf_end();

    tf_begin("algorithm/filter EMA");
    memset(&ema, 0, sizeof(ema));
    filter_ema_init(&ema, 0.5f);
    TF_CHECK(!ema.initialized);
    TF_CHECK_NEAR(filter_ema_update(&ema, 10.0f), 10.0f, 1e-4);
    TF_CHECK_NEAR(filter_ema_update(&ema, 20.0f), 15.0f, 1e-4);
    TF_CHECK_NEAR(filter_ema_update(&ema, 20.0f), 17.5f, 1e-4);
    filter_ema_reset(&ema);
    TF_CHECK_NEAR(filter_ema_update(&ema, 5.0f), 5.0f, 1e-4);
    TF_CHECK_NEAR(filter_ema_update(NULL, 6.0f), 6.0f, 1e-4);
    filter_ema_init(NULL, 0.5f);
    filter_ema_reset(NULL);
    tf_end();

    tf_begin("algorithm/filter 一阶低通阶跃");
    memset(&lp, 0, sizeof(lp));
    TF_CHECK_INT(filter_lowpass_init(&lp, 10.0f, 100.0f), 0);
    TF_CHECK_NEAR(filter_lowpass_update(&lp, 100.0f), 100.0f, 1e-4);
    TF_CHECK_NEAR(filter_lowpass_update(&lp, 0.0f), 61.4132f, 1e-2);
    TF_CHECK(filter_lowpass_update(&lp, 0.0f) < 61.4132f);
    TF_CHECK_INT(filter_lowpass_init(&lp, 0.0f, 100.0f), -1);
    TF_CHECK_INT(filter_lowpass_init(&lp, 10.0f, 0.0f), -1);
    TF_CHECK_INT(filter_lowpass_init(NULL, 10.0f, 100.0f), -1);
    TF_CHECK_NEAR(filter_lowpass_update(NULL, 3.0f), 3.0f, 1e-4);
    tf_end();

    tf_begin("algorithm/filter 一阶高通");
    memset(&hp, 0, sizeof(hp));
    TF_CHECK_INT(filter_highpass_init(&hp, 10.0f, 100.0f), 0);
    TF_CHECK_NEAR(filter_highpass_update(&hp, 100.0f), 0.0f, 1e-4);
    TF_CHECK_NEAR(filter_highpass_update(&hp, 100.0f), 0.0f, 1e-4);
    TF_CHECK_NEAR(filter_highpass_update(&hp, 0.0f), -61.4132f, 1e-2);
    TF_CHECK_INT(filter_highpass_init(&hp, -1.0f, 100.0f), -1);
    TF_CHECK_INT(filter_highpass_init(&hp, 10.0f, -100.0f), -1);
    TF_CHECK_INT(filter_highpass_init(NULL, 10.0f, 100.0f), -1);
    TF_CHECK_NEAR(filter_highpass_update(NULL, 3.0f), 3.0f, 1e-4);
    tf_end();

    tf_begin("algorithm/filter 变化率限幅");
    memset(&lim, 0, sizeof(lim));
    filter_limit_init(&lim, 1.0f);
    TF_CHECK_NEAR(filter_limit_update(&lim, 5.0f), 5.0f, 1e-4);
    TF_CHECK_NEAR(filter_limit_update(&lim, 10.0f), 6.0f, 1e-4);
    TF_CHECK_NEAR(filter_limit_update(&lim, 3.0f), 5.0f, 1e-4);
    TF_CHECK_NEAR(filter_limit_update(&lim, -10.0f), 4.0f, 1e-4);
    TF_CHECK_NEAR(filter_limit_update(&lim, -10.0f), 3.0f, 1e-4);
    filter_limit_init(&lim, 0.0f);
    TF_CHECK_NEAR(filter_limit_update(&lim, 2.0f), 2.0f, 1e-4);
    TF_CHECK_NEAR(filter_limit_update(&lim, 9.0f), 2.0f, 1e-4);
    filter_limit_init(NULL, 1.0f);
    TF_CHECK_NEAR(filter_limit_update(NULL, 4.0f), 4.0f, 1e-4);
    tf_end();

    tf_begin("algorithm/filter 卡尔曼");
    memset(&kal, 0, sizeof(kal));
    filter_kalman_init(&kal, 0.01f, 1.0f, 0.0f);
    TF_CHECK_NEAR(kal.x, 0.0f, 1e-6);
    TF_CHECK_NEAR(kal.p, 1.0f, 1e-6);
    TF_CHECK_NEAR(filter_kalman_update(&kal, 10.0f), 5.0249f, 1e-2);
    TF_CHECK(kal.k > 0.0f && kal.k < 1.0f);
    TF_CHECK_NEAR(filter_kalman_update(&kal, 10.0f), 6.7106f, 1e-2);
    TF_CHECK_NEAR(filter_kalman_update(NULL, 7.0f), 7.0f, 1e-4);
    filter_kalman_init(NULL, 0.0f, 1.0f, 0.0f);
    tf_end();

    tf_begin("algorithm/filter 滞回");
    memset(&hys, 0, sizeof(hys));
    filter_hysteresis_init(&hys, 30.0f, 70.0f);
    TF_CHECK(filter_hysteresis_update(&hys, 80.0f) == true);
    TF_CHECK(filter_hysteresis_update(&hys, 50.0f) == true);
    TF_CHECK(filter_hysteresis_update(&hys, 20.0f) == false);
    TF_CHECK(filter_hysteresis_update(&hys, 50.0f) == false);
    TF_CHECK(filter_hysteresis_update(&hys, 80.0f) == true);

    filter_hysteresis_init(&hys, 30.0f, 70.0f);
    TF_CHECK(filter_hysteresis_update(&hys, 50.0f) == false);
    TF_CHECK(filter_hysteresis_update(&hys, 70.0f) == true);
    TF_CHECK(filter_hysteresis_update(&hys, 30.0f) == false);
    TF_CHECK(filter_hysteresis_update(NULL, 100.0f) == false);
    filter_hysteresis_init(NULL, 0.0f, 1.0f);
    tf_end();

    tf_begin("algorithm/filter 零输入与 NaN 传播");
    filter_ema_init(&ema, 0.5f);
    TF_CHECK_NEAR(filter_ema_update(&ema, 0.0f), 0.0f, 1e-6);
    TF_CHECK_NEAR(filter_ema_update(&ema, 0.0f), 0.0f, 1e-6);

    filter_mean_init(&mean, mean_buf, 3);
    TF_CHECK_NEAR(filter_mean_update(&mean, 0.0f), 0.0f, 1e-6);
    TF_CHECK_NEAR(filter_mean_update(&mean, 0.0f), 0.0f, 1e-6);

    filter_ema_init(&ema, 0.5f);
    filter_ema_update(&ema, 1.0f);
    TF_CHECK(isnan(filter_ema_update(&ema, (float)NAN)) != 0);
    filter_mean_reset(&mean);
    TF_CHECK(isnan(filter_mean_update(&mean, (float)NAN)) != 0);
    tf_end();
}
