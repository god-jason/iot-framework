/*
 * filter_test.c - 数字滤波测试（对应 filter.c）
 *
 * 覆盖：滑动均值、中值、EMA、一阶低通/高通、变化率限幅、
 *       一维卡尔曼、滞回比较器与 NULL 安全。
 */

#include "test.h"
#include "filter.h"

void ut_filter(void)
{
    tf_begin("filter/滑动均值与中值");

    {
        filter_mean_t mean;
        float buf[4];
        int i;

        TF_CHECK_INT(filter_mean_init(&mean, buf, 4U), 0);
        TF_CHECK_INT(filter_mean_init(NULL, buf, 4U), -1);
        TF_CHECK_INT(filter_mean_init(&mean, NULL, 4U), -1);
        TF_CHECK_INT(filter_mean_init(&mean, buf, 0U), -1);

        TF_CHECK_INT(filter_mean_init(&mean, buf, 2U), 0);
        TF_CHECK_NEAR(filter_mean_update(&mean, 1.0f), 1.0f, 1e-6);
        TF_CHECK_NEAR(filter_mean_update(&mean, 3.0f), 2.0f, 1e-6);
        TF_CHECK_NEAR(filter_mean_update(&mean, 5.0f), 4.0f, 1e-6);
        filter_mean_reset(&mean);
        TF_CHECK_NEAR(filter_mean_update(&mean, 8.0f), 8.0f, 1e-6);
        TF_CHECK_NEAR(filter_mean_update(NULL, 2.5f), 2.5f, 1e-6);

        /* 中值：窗口 3，抑制单点尖峰 */
        {
            filter_median_t med;
            float mbuf[3];
            float work[3];

            TF_CHECK_INT(filter_median_init(&med, mbuf, work, 3U), 0);
            TF_CHECK_NEAR(filter_median_update(&med, 10.0f), 10.0f, 1e-6);
            /* 窗口未满时取当前样本集的中值 */
            TF_CHECK(filter_median_update(&med, 12.0f) >= 10.0f);
            TF_CHECK_NEAR(filter_median_update(&med, 1000.0f), 12.0f, 1e-6);
            TF_CHECK_NEAR(filter_median_update(&med, 11.0f), 12.0f, 1e-6);
            (void)i;
        }
    }

    tf_end();

    tf_begin("filter/EMA 与低通高通");

    {
        filter_ema_t ema;
        filter_lowpass_t lp;
        filter_highpass_t hp;

        filter_ema_init(&ema, 0.5f);
        TF_CHECK_NEAR(filter_ema_update(&ema, 10.0f), 10.0f, 1e-6);
        TF_CHECK_NEAR(filter_ema_update(&ema, 20.0f), 15.0f, 1e-6);
        filter_ema_reset(&ema);
        TF_CHECK_NEAR(filter_ema_update(&ema, 4.0f), 4.0f, 1e-6);

        TF_CHECK_INT(filter_lowpass_init(&lp, 10.0f, 1000.0f), 0);
        TF_CHECK_INT(filter_lowpass_init(&lp, 0.0f, 1000.0f), -1);
        TF_CHECK_INT(filter_lowpass_init(&lp, 10.0f, 0.0f), -1);
        TF_CHECK_NEAR(filter_lowpass_update(&lp, 5.0f), 5.0f, 1e-6);
        /* 阶跃响应应单调逼近，不超调 */
        {
            float y = 0.0f;
            int k;
            for (k = 0; k < 100; k++) {
                y = filter_lowpass_update(&lp, 10.0f);
                TF_CHECK(y <= 10.0f + 1e-4f);
            }
            TF_CHECK(y > 9.0f);
        }

        TF_CHECK_INT(filter_highpass_init(&hp, 10.0f, 1000.0f), 0);
        /* 首次调用返回 0 */
        TF_CHECK_NEAR(filter_highpass_update(&hp, 5.0f), 0.0f, 1e-6);
        /* 恒定输入下高通输出应趋近 0 */
        {
            float y = 0.0f;
            int k;
            for (k = 0; k < 200; k++) {
                y = filter_highpass_update(&hp, 5.0f);
            }
            TF_CHECK_NEAR(y, 0.0f, 1e-3);
        }
    }

    tf_end();

    tf_begin("filter/限幅与卡尔曼");

    {
        filter_limit_t lim;
        filter_kalman_t kal;

        filter_limit_init(&lim, 1.0f);
        TF_CHECK_NEAR(filter_limit_update(&lim, 10.0f), 10.0f, 1e-6);
        TF_CHECK_NEAR(filter_limit_update(&lim, 20.0f), 11.0f, 1e-6);
        TF_CHECK_NEAR(filter_limit_update(&lim, 0.0f), 10.0f, 1e-6);
        TF_CHECK_NEAR(filter_limit_update(NULL, 3.0f), 3.0f, 1e-6);

        filter_kalman_init(&kal, 0.01f, 1.0f, 0.0f);
        TF_CHECK_NEAR(filter_kalman_update(&kal, 10.0f), 10.0f, 5.0f);
        {
            float x = 0.0f;
            int k;
            for (k = 0; k < 200; k++) {
                x = filter_kalman_update(&kal, 10.0f);
            }
            TF_CHECK_NEAR(x, 10.0f, 0.2);
        }
        TF_CHECK_NEAR(filter_kalman_update(NULL, 7.0f), 7.0f, 1e-6);
    }

    tf_end();

    tf_begin("filter/滞回比较器");

    {
        filter_hysteresis_t hy;

        filter_hysteresis_init(&hy, 2.0f, 8.0f);
        TF_CHECK(!filter_hysteresis_update(&hy, 0.0f));
        TF_CHECK(!filter_hysteresis_update(&hy, 5.0f));   /* 区间内保持 */
        TF_CHECK(filter_hysteresis_update(&hy, 9.0f));
        TF_CHECK(filter_hysteresis_update(&hy, 5.0f));    /* 仍保持置位 */
        TF_CHECK(!filter_hysteresis_update(&hy, 1.0f));
        TF_CHECK(!filter_hysteresis_update(NULL, 100.0f));
    }

    tf_end();
}
