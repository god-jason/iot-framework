/* 电机组件测试：速度限幅、方向状态机、停止与错误路径。 */
#include "test.h"
#include "motor.h"

static int32_t s_hw_speed;
static motor_direction_e s_hw_dir;
static int s_speed_calls;
static int s_dir_calls;
static int s_stop_calls;
static int s_speed_fail;
static int s_dir_fail;

static int fake_set_speed(void *ctx, int32_t speed)
{
    (void)ctx;
    s_speed_calls++;
    if (s_speed_fail) {
        return -1;
    }
    s_hw_speed = speed;
    return 0;
}

static int fake_set_dir(void *ctx, motor_direction_e direction)
{
    (void)ctx;
    s_dir_calls++;
    if (s_dir_fail) {
        return -1;
    }
    s_hw_dir = direction;
    return 0;
}

static int fake_stop(void *ctx)
{
    (void)ctx;
    s_stop_calls++;
    return 0;
}

static const motor_ops_t s_ops = {
    fake_set_dir, fake_set_speed, fake_stop, NULL
};

static const motor_ops_t s_ops_no_stop = {
    fake_set_dir, fake_set_speed, NULL, NULL
};

static const motor_ops_t s_ops_no_dir = {
    NULL, fake_set_speed, NULL, NULL
};

static void reset_mock(void)
{
    s_hw_speed = 0;
    s_hw_dir = MOTOR_STOPPED;
    s_speed_calls = 0;
    s_dir_calls = 0;
    s_stop_calls = 0;
    s_speed_fail = 0;
    s_dir_fail = 0;
}

void ut_motor(void)
{
    motor_t motor;

    tf_begin("motor/初始化参数校验");
    TF_CHECK_INT(motor_init(NULL, &s_ops), -1);
    TF_CHECK_INT(motor_init(&motor, NULL), -1);
    tf_end();

    tf_begin("motor/初始化默认状态");
    reset_mock();
    TF_CHECK_INT(motor_init(&motor, &s_ops), 0);
    TF_CHECK_INT(motor.speed, 0);
    TF_CHECK_INT(motor.max_speed, 1000);
    TF_CHECK_INT(motor.direction, MOTOR_STOPPED);
    TF_CHECK_INT(s_speed_calls, 0);
    tf_end();

    tf_begin("motor/未初始化且缺回调时被拒绝");
    TF_CHECK_INT(motor_set_speed(NULL, 100), -1);
    motor.ops = NULL;
    TF_CHECK_INT(motor_set_speed(&motor, 100), -1);
    TF_CHECK_INT(motor_set_direction(&motor, MOTOR_FORWARD), -1);
    TF_CHECK_INT(motor_stop(&motor), -1);
    tf_end();

    tf_begin("motor/速度限幅与方向自动切换");
    reset_mock();
    TF_CHECK_INT(motor_init(&motor, &s_ops), 0);

    TF_CHECK_INT(motor_set_speed(&motor, 1500), 0);
    TF_CHECK_INT(motor.speed, 1000);
    TF_CHECK_INT(s_hw_speed, 1000);
    TF_CHECK_INT(motor.direction, MOTOR_FORWARD);

    TF_CHECK_INT(motor_set_speed(&motor, -2000), 0);
    TF_CHECK_INT(motor.speed, -1000);
    TF_CHECK_INT(s_hw_speed, -1000);
    /* 已离开 STOPPED，方向不会被速度改写 */
    TF_CHECK_INT(motor.direction, MOTOR_FORWARD);

    /* 速度 0 不改变方向（与源码一致） */
    TF_CHECK_INT(motor_set_speed(&motor, 0), 0);
    TF_CHECK_INT(motor.speed, 0);
    TF_CHECK_INT(motor.direction, MOTOR_FORWARD);
    tf_end();

    tf_begin("motor/自定义最大速度");
    reset_mock();
    TF_CHECK_INT(motor_init(&motor, &s_ops), 0);
    motor.max_speed = 300;
    TF_CHECK_INT(motor_set_speed(&motor, 500), 0);
    TF_CHECK_INT(motor.speed, 300);
    TF_CHECK_INT(s_hw_speed, 300);
    TF_CHECK_INT(motor_set_speed(&motor, -500), 0);
    TF_CHECK_INT(motor.speed, -300);
    TF_CHECK_INT(s_hw_speed, -300);
    tf_end();

    tf_begin("motor/停止后方向复位");
    reset_mock();
    TF_CHECK_INT(motor_init(&motor, &s_ops), 0);
    TF_CHECK_INT(motor_set_speed(&motor, 200), 0);
    TF_CHECK_INT(motor_stop(&motor), 0);
    TF_CHECK_INT(s_stop_calls, 1);
    TF_CHECK_INT(motor.speed, 0);
    TF_CHECK_INT(motor.direction, MOTOR_STOPPED);

    /* 停止后负速度切换为反向 */
    TF_CHECK_INT(motor_set_speed(&motor, -100), 0);
    TF_CHECK_INT(motor.direction, MOTOR_REVERSE);
    tf_end();

    tf_begin("motor/设置方向");
    reset_mock();
    TF_CHECK_INT(motor_init(&motor, &s_ops), 0);
    TF_CHECK_INT(motor_set_direction(NULL, MOTOR_FORWARD), -1);
    TF_CHECK_INT(motor_set_direction(&motor, MOTOR_FORWARD), 0);
    TF_CHECK_INT(motor.direction, MOTOR_FORWARD);
    TF_CHECK_INT(s_hw_dir, MOTOR_FORWARD);

    s_dir_fail = 1;
    TF_CHECK_INT(motor_set_direction(&motor, MOTOR_REVERSE), -1);
    TF_CHECK_INT(motor.direction, MOTOR_FORWARD);
    TF_CHECK_INT(s_hw_dir, MOTOR_FORWARD);
    s_dir_fail = 0;

    TF_CHECK_INT(motor_set_direction(&motor, MOTOR_BRAKING), 0);
    TF_CHECK_INT(motor.direction, MOTOR_BRAKING);
    tf_end();

    tf_begin("motor/底层速度设置失败不更新状态");
    reset_mock();
    TF_CHECK_INT(motor_init(&motor, &s_ops), 0);
    s_speed_fail = 1;
    TF_CHECK_INT(motor_set_speed(&motor, 400), -1);
    TF_CHECK_INT(motor.speed, 0);
    TF_CHECK_INT(motor.direction, MOTOR_STOPPED);
    s_speed_fail = 0;
    tf_end();

    tf_begin("motor/缺少 stop 回调时退化为速度 0");
    reset_mock();
    TF_CHECK_INT(motor_init(&motor, &s_ops_no_stop), 0);
    TF_CHECK_INT(motor_set_speed(&motor, 600), 0);
    TF_CHECK_INT(motor_stop(&motor), 0);
    TF_CHECK_INT(s_stop_calls, 0);
    TF_CHECK_INT(s_speed_calls, 2);
    TF_CHECK_INT(s_hw_speed, 0);
    TF_CHECK_INT(motor.speed, 0);
    TF_CHECK_INT(motor.direction, MOTOR_STOPPED);
    tf_end();

    tf_begin("motor/缺少 set_direction 回调");
    reset_mock();
    TF_CHECK_INT(motor_init(&motor, &s_ops_no_dir), 0);
    TF_CHECK_INT(motor_set_direction(&motor, MOTOR_FORWARD), -1);
    TF_CHECK_INT(motor.direction, MOTOR_STOPPED);
    /* 速度控制仍可用 */
    TF_CHECK_INT(motor_set_speed(&motor, 250), 0);
    TF_CHECK_INT(motor.speed, 250);
    tf_end();
}
