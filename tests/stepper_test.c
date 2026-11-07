/* 步进电机组件测试：方向、相对/绝对移动、位置跟踪与步进失败。 */
#include "test_framework.h"
#include "stepper.h"

static int s_steps;
static uint8_t s_hw_dir;
static int s_dir_calls;
static int s_dir_fail;
static int s_fail_at_step;   /* 0 表示不失败 */

static int fake_set_dir(void *ctx, uint8_t dir)
{
    (void)ctx;
    s_dir_calls++;
    if (s_dir_fail) {
        return -1;
    }
    s_hw_dir = dir;
    return IOT_OK;
}

static int fake_step(void *ctx)
{
    (void)ctx;
    s_steps++;
    if (s_fail_at_step != 0 && s_steps == s_fail_at_step) {
        return -1;
    }
    return IOT_OK;
}

static const stepper_ops_t s_ops = { fake_set_dir, fake_step, NULL };
static const stepper_ops_t s_ops_no_dir = { NULL, fake_step, NULL };

static void reset_mock(void)
{
    s_steps = 0;
    s_hw_dir = 0xFF;
    s_dir_calls = 0;
    s_dir_fail = 0;
    s_fail_at_step = 0;
}

void ut_stepper(void)
{
    stepper_t st;
    stepper_ops_t bad_ops;
    int32_t pos;

    tf_begin("stepper/初始化参数校验");
    TF_CHECK_INT(stepper_init(NULL, &s_ops), -1);
    TF_CHECK_INT(stepper_init(&st, NULL), -1);
    bad_ops.set_dir = NULL;
    bad_ops.step = NULL;
    bad_ops.ctx = NULL;
    TF_CHECK_INT(stepper_init(&st, &bad_ops), -1);
    tf_end();

    tf_begin("stepper/初始化默认状态");
    reset_mock();
    TF_CHECK_INT(stepper_init(&st, &s_ops), IOT_OK);
    TF_CHECK_INT(st.position, 0);
    TF_CHECK_INT(st.direction, 0);
    tf_end();

    tf_begin("stepper/方向设置与掩码");
    reset_mock();
    TF_CHECK_INT(stepper_init(&st, &s_ops), IOT_OK);
    TF_CHECK_INT(stepper_set_direction(NULL, 1), -1);

    TF_CHECK_INT(stepper_set_direction(&st, 1), IOT_OK);
    TF_CHECK_INT(st.direction, 1);
    TF_CHECK_INT(s_hw_dir, 1);

    /* 只保留最低位 */
    TF_CHECK_INT(stepper_set_direction(&st, 2), IOT_OK);
    TF_CHECK_INT(st.direction, 0);
    TF_CHECK_INT(s_hw_dir, 0);

    TF_CHECK_INT(stepper_set_direction(&st, 3), IOT_OK);
    TF_CHECK_INT(st.direction, 1);
    TF_CHECK_INT(s_hw_dir, 1);
    tf_end();

    tf_begin("stepper/正向与反向移动");
    reset_mock();
    TF_CHECK_INT(stepper_init(&st, &s_ops), IOT_OK);

    TF_CHECK_INT(stepper_move(&st, 0), IOT_OK);
    TF_CHECK_INT(st.position, 0);
    TF_CHECK_INT(s_steps, 0);
    TF_CHECK_INT(s_hw_dir, 0);

    TF_CHECK_INT(stepper_move(&st, 5), IOT_OK);
    TF_CHECK_INT(st.position, 5);
    TF_CHECK_INT(s_steps, 5);
    TF_CHECK_INT(s_hw_dir, 0);

    TF_CHECK_INT(stepper_move(&st, -3), IOT_OK);
    TF_CHECK_INT(st.position, 2);
    TF_CHECK_INT(s_steps, 8);
    TF_CHECK_INT(s_hw_dir, 1);
    tf_end();

    tf_begin("stepper/绝对位置移动");
    reset_mock();
    TF_CHECK_INT(stepper_init(&st, &s_ops), IOT_OK);
    TF_CHECK_INT(stepper_move_to(&st, 10), IOT_OK);
    TF_CHECK_INT(st.position, 10);
    TF_CHECK_INT(s_steps, 10);

    TF_CHECK_INT(stepper_move_to(&st, 4), IOT_OK);
    TF_CHECK_INT(st.position, 4);
    TF_CHECK_INT(s_steps, 16);
    TF_CHECK_INT(s_hw_dir, 1);

    /* 已在目标位置时不发脉冲 */
    TF_CHECK_INT(stepper_move_to(&st, 4), IOT_OK);
    TF_CHECK_INT(s_steps, 16);
    tf_end();

    tf_begin("stepper/读取位置");
    TF_CHECK_INT(stepper_get_position(NULL, &pos), -1);
    TF_CHECK_INT(stepper_get_position(&st, NULL), -1);
    pos = -1;
    TF_CHECK_INT(stepper_get_position(&st, &pos), IOT_OK);
    TF_CHECK_INT(pos, 4);
    tf_end();

    tf_begin("stepper/移动参数校验");
    TF_CHECK_INT(stepper_move(NULL, 1), -1);
    TF_CHECK_INT(stepper_move_to(NULL, 1), -1);
    tf_end();

    tf_begin("stepper/步进脉冲失败中断");
    reset_mock();
    TF_CHECK_INT(stepper_init(&st, &s_ops), IOT_OK);
    s_fail_at_step = 3;
    TF_CHECK_INT(stepper_move(&st, 5), -1);
    TF_CHECK_INT(s_steps, 3);
    TF_CHECK_INT(st.position, 2);   /* 前两步成功 */
    tf_end();

    tf_begin("stepper/方向设置失败则不移动");
    reset_mock();
    TF_CHECK_INT(stepper_init(&st, &s_ops), IOT_OK);
    s_dir_fail = 1;
    TF_CHECK_INT(stepper_move(&st, 3), -1);
    TF_CHECK_INT(s_steps, 0);
    TF_CHECK_INT(st.position, 0);
    s_dir_fail = 0;
    tf_end();

    tf_begin("stepper/缺少 set_dir 回调仍可移动");
    reset_mock();
    TF_CHECK_INT(stepper_init(&st, &s_ops_no_dir), IOT_OK);
    TF_CHECK_INT(stepper_move(&st, 4), IOT_OK);
    TF_CHECK_INT(st.position, 4);
    TF_CHECK_INT(s_steps, 4);
    TF_CHECK_INT(s_dir_calls, 0);
    TF_CHECK_INT(stepper_move(&st, -2), IOT_OK);
    TF_CHECK_INT(st.position, 2);
    TF_CHECK_INT(s_steps, 6);
    tf_end();
}
