/*
 * action_test.c - 动作生命周期测试（对应 action.c）
 *
 * 覆盖：状态迁移（IDLE/RUNNING/SUCCESS/FAILED/CANCELLED）、
 *       start/update 失败路径、cancel/reset、NULL 安全。
 */

#include "test.h"
#include "action.h"

typedef struct {
    int start_result;
    int update_calls;
    int update_result;
    int cancel_calls;
    int reset_calls;
} action_ctx_t;

static int act_start(action_t *a)
{
    return ((action_ctx_t *)a->ctx)->start_result;
}

static int act_update(action_t *a)
{
    action_ctx_t *c = (action_ctx_t *)a->ctx;
    c->update_calls++;
    return c->update_result;
}

static int act_cancel(action_t *a)
{
    ((action_ctx_t *)a->ctx)->cancel_calls++;
    return 0;
}

static int act_reset(action_t *a)
{
    ((action_ctx_t *)a->ctx)->reset_calls++;
    return 0;
}

void ut_robot_action(void)
{
    action_ctx_t ctx;
    action_t act;

    memset(&ctx, 0, sizeof(ctx));
    memset(&act, 0, sizeof(act));
    act.name = "test";
    act.start = act_start;
    act.update = act_update;
    act.cancel = act_cancel;
    act.reset = act_reset;
    act.ctx = &ctx;

    tf_begin("action/成功路径");

    TF_CHECK_INT(action_init(NULL), -1);
    TF_CHECK_INT(action_init(&act), 0);
    TF_CHECK_INT(action_get_state(&act), ACTION_IDLE);

    TF_CHECK_INT(action_start(&act), 0);
    TF_CHECK_INT(action_get_state(&act), ACTION_RUNNING);

    ctx.update_result = 0;
    TF_CHECK_INT(action_update(&act), 0);
    TF_CHECK_INT(ctx.update_calls, 1);
    TF_CHECK_INT(action_get_state(&act), ACTION_RUNNING);

    ctx.update_result = 1;                    /* >0 表示完成 */
    TF_CHECK_INT(action_update(&act), 0);
    TF_CHECK_INT(action_get_state(&act), ACTION_SUCCESS);

    /* 终态不再执行 update */
    TF_CHECK_INT(action_update(&act), -1);

    tf_end();

    tf_begin("action/失败与取消");

    ctx.update_result = -1;
    TF_CHECK_INT(action_reset(&act), 0);
    TF_CHECK_INT(ctx.reset_calls, 1);
    TF_CHECK_INT(action_get_state(&act), ACTION_IDLE);

    TF_CHECK_INT(action_start(&act), 0);
    TF_CHECK_INT(action_update(&act), 0);
    TF_CHECK_INT(action_get_state(&act), ACTION_FAILED);

    TF_CHECK_INT(action_reset(&act), 0);
    TF_CHECK_INT(action_cancel(&act), 0);
    TF_CHECK_INT(ctx.cancel_calls, 1);
    TF_CHECK_INT(action_get_state(&act), ACTION_CANCELLED);

    /* start 失败直接进入 FAILED */
    ctx.start_result = -1;
    TF_CHECK_INT(action_reset(&act), 0);
    TF_CHECK_INT(action_start(&act), -1);
    TF_CHECK_INT(action_get_state(&act), ACTION_FAILED);

    tf_end();

    tf_begin("action/NULL 与空回调");

    TF_CHECK_INT(action_start(NULL), -1);
    TF_CHECK_INT(action_update(NULL), -1);
    TF_CHECK_INT(action_cancel(NULL), -1);
    TF_CHECK_INT(action_reset(NULL), -1);
    TF_CHECK_INT(action_get_state(NULL), ACTION_IDLE);

    {
        action_t bare;

        memset(&bare, 0, sizeof(bare));
        TF_CHECK_INT(action_init(&bare), 0);
        /* 回调全为 NULL：start 视为成功，update 视为立即完成 */
        TF_CHECK_INT(action_start(&bare), 0);
        TF_CHECK_INT(action_update(&bare), 0);
        TF_CHECK_INT(action_get_state(&bare), ACTION_SUCCESS);
    }

    tf_end();
}
