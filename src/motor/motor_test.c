/*
 * motor_test.c - 电机抽象测试（对应 motor.c）
 *
 * 覆盖：初始化校验、带符号速度与限幅、方向设置、停止、
 *       回调缺失与底层失败的处理。
 */

#include "test.h"
#include "motor.h"

typedef struct {
    int last_speed;
    int last_dir;
    int stop_count;
    int speed_fail;
    int dir_fail;
} motor_sim_t;

static int sim_set_direction(void *ctx, motor_direction_e direction)
{
    motor_sim_t *s = (motor_sim_t *)ctx;
    if (s->dir_fail) {
        return -1;
    }
    s->last_dir = (int)direction;
    return 0;
}

static int sim_set_speed(void *ctx, int32_t speed)
{
    motor_sim_t *s = (motor_sim_t *)ctx;
    if (s->speed_fail) {
        return -1;
    }
    s->last_speed = (int)speed;
    return 0;
}

static int sim_stop(void *ctx)
{
    ((motor_sim_t *)ctx)->stop_count++;
    return 0;
}

void ut_motor(void)
{
    motor_sim_t sim;
    motor_t motor;
    motor_ops_t ops;

    memset(&sim, 0, sizeof(sim));
    ops.set_direction = sim_set_direction;
    ops.set_speed = sim_set_speed;
    ops.stop = sim_stop;
    ops.ctx = &sim;

    tf_begin("motor/初始化");

    TF_CHECK_INT(motor_init(NULL, &ops), -1);
    TF_CHECK_INT(motor_init(&motor, NULL), -1);
    TF_CHECK_INT(motor_init(&motor, &ops), 0);
    TF_CHECK_INT(motor.speed, 0);
    TF_CHECK_INT(motor.max_speed, 1000);
    TF_CHECK_INT(motor.direction, MOTOR_STOPPED);

    tf_end();

    tf_begin("motor/速度与限幅");

    motor.max_speed = 100;
    TF_CHECK_INT(motor_set_speed(&motor, 50), 0);
    TF_CHECK_INT(sim.last_speed, 50);
    TF_CHECK_INT(motor.speed, 50);

    TF_CHECK_INT(motor_set_speed(&motor, 500), 0);    /* 正向钳位 */
    TF_CHECK_INT(motor.speed, 100);

    TF_CHECK_INT(motor_set_speed(&motor, -500), 0);   /* 负向钳位 */
    TF_CHECK_INT(motor.speed, -100);

    sim.speed_fail = 1;
    TF_CHECK_INT(motor_set_speed(&motor, 10), -1);
    sim.speed_fail = 0;

    tf_end();

    tf_begin("motor/方向与停止");

    TF_CHECK_INT(motor_set_direction(&motor, MOTOR_FORWARD), 0);
    TF_CHECK_INT(sim.last_dir, MOTOR_FORWARD);
    TF_CHECK_INT(motor.direction, MOTOR_FORWARD);

    TF_CHECK_INT(motor_set_direction(&motor, MOTOR_REVERSE), 0);
    TF_CHECK_INT(sim.last_dir, MOTOR_REVERSE);

    sim.dir_fail = 1;
    TF_CHECK_INT(motor_set_direction(&motor, MOTOR_BRAKING), -1);
    sim.dir_fail = 0;

    TF_CHECK_INT(motor_stop(&motor), 0);
    TF_CHECK_INT(sim.stop_count, 1);
    TF_CHECK_INT(motor.speed, 0);
    TF_CHECK_INT(motor.direction, MOTOR_STOPPED);

    /* 可选回调缺失 */
    {
        motor_t m2;
        motor_ops_t only_stop;

        memset(&only_stop, 0, sizeof(only_stop));
        only_stop.stop = sim_stop;
        only_stop.ctx = &sim;
        TF_CHECK_INT(motor_init(&m2, &only_stop), 0);
        TF_CHECK_INT(motor_set_speed(&m2, 10), -1);
        TF_CHECK_INT(motor_set_direction(&m2, MOTOR_FORWARD), -1);
    }

    TF_CHECK_INT(motor_set_speed(NULL, 1), -1);
    TF_CHECK_INT(motor_stop(NULL), -1);

    tf_end();
}
