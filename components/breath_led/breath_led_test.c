/*
 * breath_led_test.c - 呼吸灯测试（对应 breath_led.c）
 *
 * 覆盖：初始化校验、启动参数、正弦占空比范围与波形对称性、停止熄灭。
 */

#include "test.h"
#include "breath_led.h"

static int s_last_duty;

static int sim_set_duty(void *ctx, uint16_t duty)
{
    (void)ctx;
    s_last_duty = (int)duty;
    return 0;
}

void ut_breath_led(void)
{
    breath_led_t led;
    breath_led_ops_t ops;
    uint16_t max_duty = 0;
    uint16_t min_duty = 1000;
    uint32_t t;

    ops.set_duty = sim_set_duty;
    ops.ctx = NULL;

    tf_begin("breath_led/初始化与启动");

    TF_CHECK_INT(breath_led_init(NULL, &ops), -1);
    TF_CHECK_INT(breath_led_init(&led, NULL), -1);
    {
        breath_led_ops_t bad;
        bad.set_duty = NULL;
        bad.ctx = NULL;
        TF_CHECK_INT(breath_led_init(&led, &bad), -1);
    }
    TF_CHECK_INT(breath_led_init(&led, &ops), 0);
    TF_CHECK_INT(breath_led_start(&led, 0U), -1);
    TF_CHECK_INT(breath_led_start(&led, 1000U), 0);
    TF_CHECK(led.active);
    TF_CHECK_INT(led.period_ms, 1000);

    tf_end();

    tf_begin("breath_led/占空比范围与波形");

    s_last_duty = -1;
    for (t = 0U; t <= 1000U; t += 10U) {
        TF_CHECK_INT(breath_led_update(&led, t), 0);
        TF_CHECK(s_last_duty >= 0 && s_last_duty <= 1000);
        if (s_last_duty > max_duty) {
            max_duty = (uint16_t)s_last_duty;
        }
        if (s_last_duty < min_duty) {
            min_duty = (uint16_t)s_last_duty;
        }
    }
    /* 一个周期内应覆盖较大动态范围 */
    TF_CHECK(max_duty >= 900U);
    TF_CHECK(min_duty <= 100U);

    /* 起始与整周期处应接近极小值 */
    TF_CHECK_INT(breath_led_update(&led, 0U), 0);
    TF_CHECK(s_last_duty <= 10);
    TF_CHECK_INT(breath_led_update(&led, 1000U), 0);
    TF_CHECK(s_last_duty <= 10);

    /* 半周期处应达到峰值 */
    TF_CHECK_INT(breath_led_update(&led, 500U), 0);
    TF_CHECK(s_last_duty >= 900);

    tf_end();

    tf_begin("breath_led/停止与非法参数");

    TF_CHECK_INT(breath_led_stop(&led), 0);
    TF_CHECK_INT(s_last_duty, 0);
    TF_CHECK(!led.active);

    TF_CHECK_INT(breath_led_stop(NULL), -1);
    TF_CHECK_INT(breath_led_start(NULL, 1000U), -1);
    TF_CHECK_INT(breath_led_update(NULL, 0U), -1);

    tf_end();
}
