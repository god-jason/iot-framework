/*
 * stepper_test.c - 步进电机测试（对应 stepper.c）
 *
 * 覆盖：初始化校验、方向设置、相对/绝对移动、位置读取、
 *       回调缺失与底层失败。
 */

#include "test.h"
#include "stepper.h"

typedef struct {
    int steps;
    int last_dir;
    int step_fail;
} step_sim_t;

static int sim_set_dir(void *ctx, uint8_t dir)
{
    step_sim_t *s = (step_sim_t *)ctx;
    s->last_dir = (int)dir;
    return 0;
}

static int sim_step(void *ctx)
{
    step_sim_t *s = (step_sim_t *)ctx;
    if (s->step_fail) {
        return -1;
    }
    s->steps++;
    return 0;
}

void ut_stepper(void)
{
    step_sim_t sim;
    stepper_t st;
    stepper_ops_t ops;
    int32_t pos = 0;

    memset(&sim, 0, sizeof(sim));
    ops.set_dir = sim_set_dir;
    ops.step = sim_step;
    ops.ctx = &sim;

    tf_begin("stepper/初始化");

    TF_CHECK_INT(stepper_init(NULL, &ops), -1);
    TF_CHECK_INT(stepper_init(&st, NULL), -1);
    {
        stepper_ops_t bad;
        bad.set_dir = sim_set_dir;
        bad.step = NULL;
        bad.ctx = &sim;
        TF_CHECK_INT(stepper_init(&st, &bad), -1);
    }
    TF_CHECK_INT(stepper_init(&st, &ops), 0);
    TF_CHECK_INT(st.position, 0);
    TF_CHECK_INT(st.direction, 0);

    tf_end();

    tf_begin("stepper/方向与相对移动");

    TF_CHECK_INT(stepper_set_direction(&st, 1U), 0);
    TF_CHECK_INT(st.direction, 1);
    TF_CHECK_INT(sim.last_dir, 1);

    TF_CHECK_INT(stepper_move(&st, 10), 0);
    TF_CHECK_INT(sim.steps, 10);
    TF_CHECK_INT(st.position, 10);

    TF_CHECK_INT(stepper_move(&st, -4), 0);          /* 负步数自动反向 */
    TF_CHECK_INT(sim.steps, 14);
    TF_CHECK_INT(st.position, 6);
    TF_CHECK_INT(st.direction, 1);

    TF_CHECK_INT(stepper_get_position(&st, &pos), 0);
    TF_CHECK_INT(pos, 6);

    tf_end();

    tf_begin("stepper/绝对移动与失败路径");

    TF_CHECK_INT(stepper_move_to(&st, 20), 0);
    TF_CHECK_INT(sim.steps, 28);
    TF_CHECK_INT(st.position, 20);

    TF_CHECK_INT(stepper_move_to(&st, 20), 0);       /* 已在目标位置 */
    TF_CHECK_INT(sim.steps, 28);

    sim.step_fail = 1;
    TF_CHECK_INT(stepper_move(&st, 3), -1);
    sim.step_fail = 0;

    TF_CHECK_INT(stepper_move(NULL, 1), -1);
    TF_CHECK_INT(stepper_move_to(NULL, 1), -1);
    TF_CHECK_INT(stepper_get_position(&st, NULL), -1);
    TF_CHECK_INT(stepper_set_direction(NULL, 0U), -1);

    tf_end();
}
