/* 舵机组件测试：角度限幅、角度到脉宽映射、直接脉宽设置与错误路径。 */
#include "test.h"
#include "servo.h"

static uint16_t s_last_pulse;
static int s_pulse_calls;
static int s_pulse_result;

static int record_pulse(void *ctx, uint16_t us)
{
    (void)ctx;
    s_last_pulse = us;
    s_pulse_calls++;
    return s_pulse_result;
}

static const servo_ops_t s_ops = { record_pulse, NULL };

void ut_servo(void)
{
    servo_t servo;
    servo_ops_t bad_ops;

    tf_begin("servo/初始化参数校验");
    TF_CHECK_INT(servo_init(NULL, &s_ops, 500, 2500, 0, 180), -1);
    TF_CHECK_INT(servo_init(&servo, NULL, 500, 2500, 0, 180), -1);
    bad_ops.set_pulse = NULL;
    bad_ops.ctx = NULL;
    TF_CHECK_INT(servo_init(&servo, &bad_ops, 500, 2500, 0, 180), -1);
    tf_end();

    tf_begin("servo/初始化默认状态");
    s_last_pulse = 0xFFFF;
    s_pulse_calls = 0;
    s_pulse_result = 0;
    TF_CHECK_INT(servo_init(&servo, &s_ops, 500, 2500, 0, 180), 0);
    TF_CHECK_INT(servo.min_pulse_us, 500);
    TF_CHECK_INT(servo.max_pulse_us, 2500);
    TF_CHECK_INT(servo.min_angle, 0);
    TF_CHECK_INT(servo.max_angle, 180);
    TF_CHECK_INT(servo.angle, 0);
    TF_CHECK_INT(s_pulse_calls, 0);
    tf_end();

    tf_begin("servo/角度到脉宽线性映射");
    TF_CHECK_INT(servo_set_angle(NULL, 90), -1);

    TF_CHECK_INT(servo_set_angle(&servo, 0), 0);
    TF_CHECK_INT(servo.angle, 0);
    TF_CHECK_INT(s_last_pulse, 500);

    TF_CHECK_INT(servo_set_angle(&servo, 90), 0);
    TF_CHECK_INT(servo.angle, 90);
    TF_CHECK_INT(s_last_pulse, 1500);

    TF_CHECK_INT(servo_set_angle(&servo, 180), 0);
    TF_CHECK_INT(servo.angle, 180);
    TF_CHECK_INT(s_last_pulse, 2500);
    tf_end();

    tf_begin("servo/角度上下限钳制");
    TF_CHECK_INT(servo_set_angle(&servo, 0), 0);

    TF_CHECK_INT(servo_set_angle(&servo, 500), 0);
    TF_CHECK_INT(servo.angle, 180);
    TF_CHECK_INT(s_last_pulse, 2500);

    TF_CHECK_INT(servo_set_angle(&servo, 0), 0);
    /* 无效角度从上限之后回落到 0 也需正确映射 */
    TF_CHECK_INT(s_last_pulse, 500);
    tf_end();

    tf_begin("servo/自定义角度范围映射");
    TF_CHECK_INT(servo_init(&servo, &s_ops, 1000, 2000, 10, 20), 0);
    TF_CHECK_INT(servo.angle, 10);

    TF_CHECK_INT(servo_set_angle(&servo, 10), 0);
    TF_CHECK_INT(s_last_pulse, 1000);

    TF_CHECK_INT(servo_set_angle(&servo, 15), 0);
    TF_CHECK_INT(s_last_pulse, 1500);

    TF_CHECK_INT(servo_set_angle(&servo, 20), 0);
    TF_CHECK_INT(s_last_pulse, 2000);

    /* 低于下限钳制到 10 */
    TF_CHECK_INT(servo_set_angle(&servo, 0), 0);
    TF_CHECK_INT(servo.angle, 10);
    TF_CHECK_INT(s_last_pulse, 1000);
    tf_end();

    tf_begin("servo/直接设置脉宽");
    s_last_pulse = 0;
    TF_CHECK_INT(servo_set_pulse(NULL, 1500), -1);
    TF_CHECK_INT(servo_set_pulse(&servo, 1200), 0);
    TF_CHECK_INT(s_last_pulse, 1200);
    tf_end();

    tf_begin("servo/底层设置失败传播");
    s_pulse_result = -1;
    TF_CHECK_INT(servo_set_angle(&servo, 15), -1);
    TF_CHECK_INT(servo.angle, 15);   /* 角度在调用底层前已更新 */
    TF_CHECK_INT(servo_set_pulse(&servo, 1000), -1);
    s_pulse_result = 0;
    tf_end();

    tf_begin("servo/重新初始化复位角度");
    TF_CHECK_INT(servo_set_angle(&servo, 20), 0);
    TF_CHECK_INT(servo_init(&servo, &s_ops, 500, 2500, 0, 180), 0);
    TF_CHECK_INT(servo.angle, 0);
    tf_end();
}
