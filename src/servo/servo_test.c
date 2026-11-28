/*
 * servo_test.c - 舵机测试（对应 servo.c）
 *
 * 覆盖：参数校验、角度到脉宽映射、角度钳位、直接设置脉宽。
 */

#include "test.h"
#include "servo.h"

static int s_last_pulse;

static int sim_set_pulse(void *ctx, uint16_t us)
{
    (void)ctx;
    s_last_pulse = (int)us;
    return 0;
}

void ut_servo(void)
{
    servo_t servo;
    servo_ops_t ops;

    ops.set_pulse = sim_set_pulse;
    ops.ctx = NULL;

    tf_begin("servo/初始化校验");

    TF_CHECK_INT(servo_init(NULL, &ops, 500U, 2500U, 0U, 180U), -1);
    TF_CHECK_INT(servo_init(&servo, NULL, 500U, 2500U, 0U, 180U), -1);

    {
        servo_ops_t bad;
        bad.set_pulse = NULL;
        bad.ctx = NULL;
        TF_CHECK_INT(servo_init(&servo, &bad, 500U, 2500U, 0U, 180U), -1);
    }

    TF_CHECK_INT(servo_init(&servo, &ops, 500U, 2500U, 0U, 180U), 0);

    tf_end();

    tf_begin("servo/角度到脉宽映射");

    TF_CHECK_INT(servo_set_angle(&servo, 0U), 0);
    TF_CHECK_INT(s_last_pulse, 500);

    TF_CHECK_INT(servo_set_angle(&servo, 180U), 0);
    TF_CHECK_INT(s_last_pulse, 2500);

    TF_CHECK_INT(servo_set_angle(&servo, 90U), 0);
    TF_CHECK_INT(s_last_pulse, 1500);
    TF_CHECK_INT(servo.angle, 90);

    tf_end();

    tf_begin("servo/钳位与直接脉宽");

    TF_CHECK_INT(servo_set_angle(&servo, 300U), 0);
    TF_CHECK_INT(s_last_pulse, 2500);
    TF_CHECK_INT(servo.angle, 180);

    TF_CHECK_INT(servo_set_pulse(&servo, 1234U), 0);
    TF_CHECK_INT(s_last_pulse, 1234);

    TF_CHECK_INT(servo_set_angle(NULL, 10U), -1);
    TF_CHECK_INT(servo_set_pulse(NULL, 1000U), -1);

    tf_end();
}
