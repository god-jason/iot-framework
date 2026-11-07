/* PWM 风扇组件测试：占空比设置、限幅、开关与错误路径。 */
#include "test_framework.h"
#include "fan.h"

static uint16_t s_last_duty;
static int s_duty_calls;
static int s_duty_result;

static int record_duty(void *ctx, uint16_t duty)
{
    (void)ctx;
    s_last_duty = duty;
    s_duty_calls++;
    return s_duty_result;
}

static const fan_ops_t s_ops = { record_duty, NULL };

void ut_fan(void)
{
    fan_t fan;
    fan_ops_t bad_ops;

    tf_begin("fan/初始化参数校验");
    TF_CHECK_INT(fan_init(NULL, &s_ops), -1);
    TF_CHECK_INT(fan_init(&fan, NULL), -1);
    bad_ops.set_duty = NULL;
    bad_ops.ctx = NULL;
    TF_CHECK_INT(fan_init(&fan, &bad_ops), -1);
    tf_end();

    tf_begin("fan/初始化后默认停止");
    s_last_duty = 0xFFFF;
    s_duty_calls = 0;
    s_duty_result = IOT_OK;
    TF_CHECK_INT(fan_init(&fan, &s_ops), IOT_OK);
    TF_CHECK_INT(fan.duty, 0);
    TF_CHECK_INT(s_duty_calls, 1);
    TF_CHECK_INT(s_last_duty, 0);
    tf_end();

    tf_begin("fan/占空比设置与限幅");
    TF_CHECK_INT(fan_set_speed(NULL, 100), -1);
    TF_CHECK_INT(fan_set_speed(&fan, 500), IOT_OK);
    TF_CHECK_INT(fan.duty, 500);
    TF_CHECK_INT(s_last_duty, 500);

    TF_CHECK_INT(fan_set_speed(&fan, 0), IOT_OK);
    TF_CHECK_INT(fan.duty, 0);
    TF_CHECK_INT(s_last_duty, 0);

    TF_CHECK_INT(fan_set_speed(&fan, 1000), IOT_OK);
    TF_CHECK_INT(fan.duty, 1000);
    TF_CHECK_INT(s_last_duty, 1000);

    /* 超上限被钳制到 1000 */
    TF_CHECK_INT(fan_set_speed(&fan, 1001), IOT_OK);
    TF_CHECK_INT(fan.duty, 1000);
    TF_CHECK_INT(s_last_duty, 1000);

    TF_CHECK_INT(fan_set_speed(&fan, 65535), IOT_OK);
    TF_CHECK_INT(fan.duty, 1000);
    TF_CHECK_INT(s_last_duty, 1000);
    tf_end();

    tf_begin("fan/开关控制");
    TF_CHECK_INT(fan_on(NULL), -1);
    TF_CHECK_INT(fan_off(NULL), -1);

    TF_CHECK_INT(fan_on(&fan), IOT_OK);
    TF_CHECK_INT(fan.duty, 1000);
    TF_CHECK_INT(s_last_duty, 1000);

    TF_CHECK_INT(fan_off(&fan), IOT_OK);
    TF_CHECK_INT(fan.duty, 0);
    TF_CHECK_INT(s_last_duty, 0);
    tf_end();

    tf_begin("fan/底层设置失败传播");
    s_duty_result = -1;
    TF_CHECK_INT(fan_set_speed(&fan, 300), -1);
    TF_CHECK_INT(fan.duty, 300);   /* 状态已在调用前更新 */
    s_duty_result = IOT_OK;
    tf_end();

    tf_begin("fan/重新初始化复位状态");
    TF_CHECK_INT(fan_set_speed(&fan, 700), IOT_OK);
    TF_CHECK_INT(fan_init(&fan, &s_ops), IOT_OK);
    TF_CHECK_INT(fan.duty, 0);
    TF_CHECK_INT(s_last_duty, 0);
    tf_end();
}
