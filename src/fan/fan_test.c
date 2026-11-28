/*
 * fan_test.c - 风扇测试（对应 fan.c）
 *
 * 覆盖：初始化归零、档位设置与钳位、全速/停止、NULL 安全、
 *       TACH 下降沿识别、轮询/中断测速与 RPM 换算。
 */

#include "test.h"
#include "fan.h"

static int s_last_duty;

static int sim_set_duty(void *ctx, uint16_t duty)
{
    (void)ctx;
    s_last_duty = (int)duty;
    return 0;
}

void ut_fan(void)
{
    fan_t fan;
    fan_ops_t ops;

    ops.set_duty = sim_set_duty;
    ops.ctx = NULL;

    tf_begin("fan/初始化");

    TF_CHECK_INT(fan_init(NULL, &ops), -1);
    TF_CHECK_INT(fan_init(&fan, NULL), -1);
    {
        fan_ops_t bad;
        bad.set_duty = NULL;
        bad.ctx = NULL;
        TF_CHECK_INT(fan_init(&fan, &bad), -1);
    }

    s_last_duty = -1;
    TF_CHECK_INT(fan_init(&fan, &ops), 0);
    TF_CHECK_INT(s_last_duty, 0);        /* 初始化即关闭 */
    TF_CHECK_INT(fan.duty, 0);

    tf_end();

    tf_begin("fan/档位与钳位");

    TF_CHECK_INT(fan_set_speed(&fan, 300U), 0);
    TF_CHECK_INT(s_last_duty, 300);
    TF_CHECK_INT(fan.duty, 300);

    TF_CHECK_INT(fan_set_speed(&fan, 1000U), 0);
    TF_CHECK_INT(s_last_duty, 1000);

    TF_CHECK_INT(fan_set_speed(&fan, 5000U), 0);   /* 超限钳位 */
    TF_CHECK_INT(s_last_duty, 1000);
    TF_CHECK_INT(fan.duty, 1000);

    TF_CHECK_INT(fan_on(&fan), 0);
    TF_CHECK_INT(s_last_duty, 1000);

    TF_CHECK_INT(fan_off(&fan), 0);
    TF_CHECK_INT(s_last_duty, 0);
    TF_CHECK_INT(fan.duty, 0);

    tf_end();

    tf_begin("fan/NULL 安全");

    TF_CHECK_INT(fan_set_speed(NULL, 100U), -1);
    TF_CHECK_INT(fan_on(NULL), -1);
    TF_CHECK_INT(fan_off(NULL), -1);

    tf_end();

    tf_begin("fan/反馈测速");
    {
        fan_t f;
        uint32_t i;

        TF_CHECK_INT(fan_init(&f, &ops), 0);
        TF_CHECK_INT(fan_get_rpm(&f), 0);
        TF_CHECK_INT(fan_get_pulse_count(&f), 0);
        TF_CHECK_INT(f.pulses_per_rev, FAN_TACH_PPR_DEFAULT);

        /* t=0 高电平建立基准 */
        TF_CHECK_INT(fan_tach_sample(&f, 1U, 0U), 0);

        /* 1s 内 10 个下降沿，ppr=2 -> 300 rpm */
        for (i = 0U; i < 10U; i++) {
            uint32_t t = i * 100U + 50U;

            (void)fan_tach_sample(&f, 0U, t);
            (void)fan_tach_sample(&f, 1U, t + 50U);
        }
        TF_CHECK_INT(fan_tach_sample(&f, 1U, 1000U), 300);
        TF_CHECK_INT(fan_get_rpm(&f), 300);
        TF_CHECK_INT(fan_get_pulse_count(&f), 10);

        /* 下一个窗口无脉冲 -> 0 rpm */
        TF_CHECK_INT(fan_tach_sample(&f, 1U, 2000U), 0);
    }
    tf_end();

    tf_begin("fan/中断脉冲计数");
    {
        fan_t f;
        uint32_t i;

        TF_CHECK_INT(fan_init(&f, &ops), 0);
        TF_CHECK_INT(fan_set_pulses_per_rev(&f, 1U), 0);
        TF_CHECK_INT(fan_update_rpm(&f, 0U), 0);   /* 建立基准 */

        for (i = 0U; i < 10U; i++) {
            fan_tach_pulse(&f);
        }
        TF_CHECK_INT(fan_get_pulse_count(&f), 10);
        /* 10 脉冲/s，ppr=1 -> 600 rpm */
        TF_CHECK_INT(fan_update_rpm(&f, 1000U), 600);
        TF_CHECK_INT(fan_update_rpm(&f, 2000U), 0);
    }
    tf_end();

    tf_begin("fan/下降沿识别");
    {
        fan_t f;

        TF_CHECK_INT(fan_init(&f, &ops), 0);

        TF_CHECK_INT(fan_tach_sample(&f, 0U, 0U), 0);    /* 首次低电平 */
        TF_CHECK_INT(fan_tach_sample(&f, 0U, 10U), 0);   /* 低->低 不计数 */
        TF_CHECK_INT(fan_get_pulse_count(&f), 0);

        TF_CHECK_INT(fan_tach_sample(&f, 1U, 20U), 0);
        TF_CHECK_INT(fan_tach_sample(&f, 0U, 30U), 0);   /* 高->低 计一 */
        TF_CHECK_INT(fan_get_pulse_count(&f), 1);
        TF_CHECK_INT(fan_tach_sample(&f, 1U, 40U), 0);   /* 上升沿不计数 */
        TF_CHECK_INT(fan_get_pulse_count(&f), 1);
    }
    tf_end();

    tf_begin("fan/测速参数与 NULL");
    {
        fan_t f;

        TF_CHECK_INT(fan_init(&f, &ops), 0);
        TF_CHECK_INT(fan_set_pulses_per_rev(NULL, 2U), -1);
        TF_CHECK_INT(fan_set_pulses_per_rev(&f, 0U), -1);
        TF_CHECK_INT(fan_set_pulses_per_rev(&f, 3U), 0);

        TF_CHECK_INT(fan_get_rpm(NULL), 0);
        TF_CHECK_INT(fan_get_pulse_count(NULL), 0);
        TF_CHECK_INT(fan_tach_sample(NULL, 1U, 0U), 0);
        TF_CHECK_INT(fan_update_rpm(NULL, 0U), 0);
        fan_tach_pulse(NULL);
    }
    tf_end();
}
