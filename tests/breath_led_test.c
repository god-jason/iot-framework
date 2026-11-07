/* 呼吸灯组件测试：初始化、启停、正弦占空比推进与状态复位。 */
#include "test_framework.h"
#include "breath_led.h"

static uint16_t s_last_duty;
static int s_duty_calls;

static int record_duty(void *ctx, uint16_t duty)
{
    (void)ctx;
    s_last_duty = duty;
    s_duty_calls++;
    return IOT_OK;
}

static const breath_led_ops_t s_ops = { record_duty, NULL };

static void reset_record(void)
{
    s_last_duty = 0xFFFF;
    s_duty_calls = 0;
}

void ut_breath_led(void)
{
    breath_led_t led;
    breath_led_ops_t bad_ops;
    breath_led_t fresh;

    tf_begin("breath_led/初始化参数校验");
    TF_CHECK_INT(breath_led_init(NULL, &s_ops), -1);
    TF_CHECK_INT(breath_led_init(&led, NULL), -1);
    bad_ops.set_duty = NULL;
    bad_ops.ctx = NULL;
    TF_CHECK_INT(breath_led_init(&led, &bad_ops), -1);
    tf_end();

    tf_begin("breath_led/初始化默认状态");
    TF_CHECK_INT(breath_led_init(&led, &s_ops), IOT_OK);
    TF_CHECK_INT(led.duty, 0);
    TF_CHECK_INT(led.period_ms, 2000);
    TF_CHECK_INT(led.start_tick, 0);
    TF_CHECK(led.started == false);
    TF_CHECK(led.active == false);
    tf_end();

    tf_begin("breath_led/启动参数校验");
    TF_CHECK_INT(breath_led_start(NULL, 1000), -1);
    reset_record();
    TF_CHECK_INT(breath_led_start(&led, 0), -1);
    TF_CHECK(led.active == false);
    TF_CHECK_INT(s_duty_calls, 0);
    tf_end();

    tf_begin("breath_led/未启动时 update 被拒绝");
    TF_CHECK_INT(breath_led_update(&led, 0), -1);
    TF_CHECK_INT(breath_led_update(NULL, 0), -1);
    tf_end();

    tf_begin("breath_led/启动与正弦占空比");
    TF_CHECK_INT(breath_led_start(&led, 1000), IOT_OK);
    TF_CHECK(led.active == true);
    TF_CHECK(led.started == false);
    TF_CHECK_INT(led.period_ms, 1000);
    reset_record();

    /* 首次 update 记录起点，相位为 0，占空比为 0 */
    TF_CHECK_INT(breath_led_update(&led, 1000), IOT_OK);
    TF_CHECK_INT(led.start_tick, 1000);
    TF_CHECK(led.started == true);
    TF_CHECK_INT(led.duty, 0);
    TF_CHECK_INT(s_last_duty, 0);
    TF_CHECK_INT(s_duty_calls, 1);

    /* 半周期：sin(pi/2) 接近 1，占空比接近 1000 */
    TF_CHECK_INT(breath_led_update(&led, 1500), IOT_OK);
    TF_CHECK(led.duty >= 990 && led.duty <= 1000);
    TF_CHECK_INT(s_last_duty, led.duty);

    /* 1/4 周期：sin(pi/4) 约 0.707 */
    TF_CHECK_INT(breath_led_update(&led, 1250), IOT_OK);
    TF_CHECK(led.duty >= 700 && led.duty <= 710);

    /* 3/4 周期：与 1/4 周期对称 */
    TF_CHECK_INT(breath_led_update(&led, 1750), IOT_OK);
    TF_CHECK(led.duty >= 700 && led.duty <= 710);
    tf_end();

    tf_begin("breath_led/停止与状态复位");
    TF_CHECK_INT(breath_led_stop(NULL), -1);
    reset_record();
    TF_CHECK_INT(breath_led_stop(&led), IOT_OK);
    TF_CHECK(led.active == false);
    TF_CHECK_INT(led.duty, 0);
    TF_CHECK_INT(s_last_duty, 0);
    TF_CHECK_INT(s_duty_calls, 1);
    /* 停止后 update 不再生效 */
    TF_CHECK_INT(breath_led_update(&led, 2000), -1);
    tf_end();

    tf_begin("breath_led/再次启动重新计时");
    TF_CHECK_INT(breath_led_start(&led, 2000), IOT_OK);
    TF_CHECK_INT(led.period_ms, 2000);
    TF_CHECK(led.started == false);
    TF_CHECK_INT(breath_led_update(&led, 5000), IOT_OK);
    TF_CHECK_INT(led.start_tick, 5000);
    TF_CHECK_INT(led.duty, 0);
    TF_CHECK_INT(breath_led_update(&led, 6000), IOT_OK);
    TF_CHECK(led.duty >= 990 && led.duty <= 1000);
    tf_end();

    tf_begin("breath_led/未初始化对象 update 被拒绝");
    memset(&fresh, 0, sizeof(fresh));
    TF_CHECK_INT(breath_led_update(&fresh, 0), -1);
    tf_end();
}
