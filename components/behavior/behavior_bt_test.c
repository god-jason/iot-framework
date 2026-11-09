/* robot/behavior 行为树单元测试：组合节点、装饰节点、延迟与复位。
 * 注意：节点来自内部 32 槽静态池且不释放，故本用例共创建 30 个节点。 */
#include "test.h"
#include "behavior_bt.h"

static int cb_success(behavior_bt_node_t *n)
{
    (void)n;
    return BEHAVIOR_BT_SUCCESS;
}

static int cb_failure(behavior_bt_node_t *n)
{
    (void)n;
    return BEHAVIOR_BT_FAILURE;
}

static int cb_running(behavior_bt_node_t *n)
{
    (void)n;
    return BEHAVIOR_BT_RUNNING;
}

static int s_rep_ticks;
static int cb_count(behavior_bt_node_t *n)
{
    (void)n;
    s_rep_ticks++;
    return BEHAVIOR_BT_SUCCESS;
}

static bool cb_true(behavior_bt_node_t *n)
{
    (void)n;
    return true;
}

static bool cb_false(behavior_bt_node_t *n)
{
    (void)n;
    return false;
}

static int s_halt_calls;
static int s_reset_calls;
static void cb_halt(behavior_bt_node_t *n)
{
    (void)n;
    s_halt_calls++;
}
static void cb_reset(behavior_bt_node_t *n)
{
    (void)n;
    s_reset_calls++;
}

void ut_robot_bt(void)
{
    behavior_bt_node_t *act_ok;
    behavior_bt_node_t *act_fail;
    behavior_bt_node_t *act_run;
    behavior_bt_node_t *act_null;
    behavior_bt_node_t *cond_t;
    behavior_bt_node_t *cond_f;
    behavior_bt_node_t *cond_null;
    behavior_bt_node_t *kids[4];
    behavior_bt_node_t *seq_ok;
    behavior_bt_node_t *seq_fail;
    behavior_bt_node_t *seq_run;
    behavior_bt_node_t *seq_empty;
    behavior_bt_node_t *sel_ok;
    behavior_bt_node_t *sel_fail;
    behavior_bt_node_t *sel_run;
    behavior_bt_node_t *sel_empty;
    behavior_bt_node_t *par_ok;
    behavior_bt_node_t *par_fail;
    behavior_bt_node_t *par_run;
    behavior_bt_node_t *inv_ok;
    behavior_bt_node_t *inv_fail;
    behavior_bt_node_t *inv_run;
    behavior_bt_node_t *inv_empty;
    behavior_bt_node_t *act_count;
    behavior_bt_node_t *rep3;
    behavior_bt_node_t *rep0;
    behavior_bt_node_t *rep_run;
    behavior_bt_node_t *rep_fail;
    behavior_bt_node_t *rep_empty;
    behavior_bt_node_t *del2;
    behavior_bt_node_t *del_empty;

    act_ok = behavior_bt_action_create(cb_success, NULL);
    act_fail = behavior_bt_action_create(cb_failure, NULL);
    act_run = behavior_bt_action_create(cb_running, NULL);
    act_null = behavior_bt_action_create(NULL, NULL);
    cond_t = behavior_bt_condition_create(cb_true, NULL);
    cond_f = behavior_bt_condition_create(cb_false, NULL);
    cond_null = behavior_bt_condition_create(NULL, NULL);

    tf_begin("robot/bt 动作与条件节点");
    TF_CHECK(act_ok != NULL);
    TF_CHECK_INT(act_ok->type, BEHAVIOR_BT_ACTION);
    TF_CHECK_INT(behavior_bt_tick(act_ok), BEHAVIOR_BT_SUCCESS);
    TF_CHECK_INT(behavior_bt_tick(act_fail), BEHAVIOR_BT_FAILURE);
    TF_CHECK_INT(behavior_bt_tick(act_run), BEHAVIOR_BT_RUNNING);
    TF_CHECK_INT(behavior_bt_tick(act_null), BEHAVIOR_BT_FAILURE);
    TF_CHECK_INT(behavior_bt_tick(cond_t), BEHAVIOR_BT_SUCCESS);
    TF_CHECK_INT(behavior_bt_tick(cond_f), BEHAVIOR_BT_FAILURE);
    TF_CHECK_INT(behavior_bt_tick(cond_null), BEHAVIOR_BT_FAILURE);
    tf_end();

    tf_begin("robot/bt 顺序节点");
    kids[0] = act_ok;
    kids[1] = act_ok;
    seq_ok = behavior_bt_sequence_create(kids, 2);
    TF_CHECK(seq_ok != NULL);
    TF_CHECK_INT(behavior_bt_tick(seq_ok), BEHAVIOR_BT_SUCCESS);

    kids[0] = act_ok;
    kids[1] = act_fail;
    seq_fail = behavior_bt_sequence_create(kids, 2);
    TF_CHECK_INT(behavior_bt_tick(seq_fail), BEHAVIOR_BT_FAILURE);

    kids[0] = act_run;
    kids[1] = act_ok;
    seq_run = behavior_bt_sequence_create(kids, 2);
    TF_CHECK_INT(behavior_bt_tick(seq_run), BEHAVIOR_BT_RUNNING);

    seq_empty = behavior_bt_sequence_create(NULL, 0);
    TF_CHECK_INT(behavior_bt_tick(seq_empty), BEHAVIOR_BT_SUCCESS);
    tf_end();

    tf_begin("robot/bt 选择节点");
    kids[0] = act_fail;
    kids[1] = act_ok;
    sel_ok = behavior_bt_selector_create(kids, 2);
    TF_CHECK_INT(behavior_bt_tick(sel_ok), BEHAVIOR_BT_SUCCESS);

    kids[0] = act_fail;
    kids[1] = act_fail;
    sel_fail = behavior_bt_selector_create(kids, 2);
    TF_CHECK_INT(behavior_bt_tick(sel_fail), BEHAVIOR_BT_FAILURE);

    kids[0] = act_fail;
    kids[1] = act_run;
    sel_run = behavior_bt_selector_create(kids, 2);
    TF_CHECK_INT(behavior_bt_tick(sel_run), BEHAVIOR_BT_RUNNING);

    sel_empty = behavior_bt_selector_create(NULL, 0);
    TF_CHECK_INT(behavior_bt_tick(sel_empty), BEHAVIOR_BT_FAILURE);
    tf_end();

    tf_begin("robot/bt 并行节点");
    kids[0] = act_ok;
    kids[1] = act_ok;
    par_ok = behavior_bt_parallel_create(kids, 2);
    TF_CHECK_INT(behavior_bt_tick(par_ok), BEHAVIOR_BT_SUCCESS);

    kids[0] = act_ok;
    kids[1] = act_fail;
    par_fail = behavior_bt_parallel_create(kids, 2);
    TF_CHECK_INT(behavior_bt_tick(par_fail), BEHAVIOR_BT_FAILURE);

    kids[0] = act_ok;
    kids[1] = act_run;
    par_run = behavior_bt_parallel_create(kids, 2);
    TF_CHECK_INT(behavior_bt_tick(par_run), BEHAVIOR_BT_RUNNING);
    tf_end();

    tf_begin("robot/bt 取反节点");
    kids[0] = act_ok;
    inv_ok = behavior_bt_inverter_create(kids, 1);
    TF_CHECK_INT(behavior_bt_tick(inv_ok), BEHAVIOR_BT_FAILURE);

    kids[0] = act_fail;
    inv_fail = behavior_bt_inverter_create(kids, 1);
    TF_CHECK_INT(behavior_bt_tick(inv_fail), BEHAVIOR_BT_SUCCESS);

    kids[0] = act_run;
    inv_run = behavior_bt_inverter_create(kids, 1);
    TF_CHECK_INT(behavior_bt_tick(inv_run), BEHAVIOR_BT_RUNNING);

    inv_empty = behavior_bt_inverter_create(NULL, 0);
    TF_CHECK_INT(behavior_bt_tick(inv_empty), BEHAVIOR_BT_FAILURE);
    tf_end();

    tf_begin("robot/bt 重复节点");
    act_count = behavior_bt_action_create(cb_count, NULL);
    kids[0] = act_count;
    rep3 = behavior_bt_repeat_create(kids, 1, 3);
    s_rep_ticks = 0;
    TF_CHECK_INT(behavior_bt_tick(rep3), BEHAVIOR_BT_SUCCESS);
    TF_CHECK_INT(s_rep_ticks, 3);
    TF_CHECK_INT(rep3->counter, -1);

    kids[0] = act_count;
    rep0 = behavior_bt_repeat_create(kids, 1, 0);
    s_rep_ticks = 0;
    TF_CHECK_INT(behavior_bt_tick(rep0), BEHAVIOR_BT_SUCCESS);
    TF_CHECK_INT(s_rep_ticks, 0);

    kids[0] = act_run;
    rep_run = behavior_bt_repeat_create(kids, 1, 2);
    TF_CHECK_INT(behavior_bt_tick(rep_run), BEHAVIOR_BT_RUNNING);
    TF_CHECK_INT(rep_run->counter, 2);

    kids[0] = act_fail;
    rep_fail = behavior_bt_repeat_create(kids, 1, 2);
    TF_CHECK_INT(behavior_bt_tick(rep_fail), BEHAVIOR_BT_FAILURE);
    TF_CHECK_INT(rep_fail->counter, -1);

    rep_empty = behavior_bt_repeat_create(NULL, 0, 2);
    TF_CHECK_INT(behavior_bt_tick(rep_empty), BEHAVIOR_BT_FAILURE);
    tf_end();

    tf_begin("robot/bt 延迟节点");
    kids[0] = act_ok;
    del2 = behavior_bt_delay_create(kids, 1, 2);
    TF_CHECK(behavior_bt_init(del2) == 0);
    TF_CHECK_INT(behavior_bt_tick(del2), BEHAVIOR_BT_RUNNING);
    TF_CHECK_INT(behavior_bt_tick(del2), BEHAVIOR_BT_RUNNING);
    TF_CHECK_INT(behavior_bt_tick(del2), BEHAVIOR_BT_SUCCESS);

    del_empty = behavior_bt_delay_create(NULL, 0, 1);
    TF_CHECK_INT(behavior_bt_tick(del_empty), BEHAVIOR_BT_FAILURE);
    tf_end();

    tf_begin("robot/bt 停止与复位传播");
    s_halt_calls = 0;
    s_reset_calls = 0;
    act_ok->halt = cb_halt;
    act_ok->reset = cb_reset;
    kids[0] = act_ok;
    kids[1] = act_ok;
    TF_CHECK_INT(behavior_bt_halt(seq_ok), 0);
    TF_CHECK_INT(s_halt_calls, 2);
    TF_CHECK_INT(behavior_bt_reset(seq_ok), 0);
    TF_CHECK_INT(s_reset_calls, 2);
    tf_end();

    tf_begin("robot/bt 空指针保护");
    TF_CHECK_INT(behavior_bt_init(NULL), -1);
    TF_CHECK_INT(behavior_bt_tick(NULL), BEHAVIOR_BT_FAILURE);
    TF_CHECK_INT(behavior_bt_halt(NULL), -1);
    TF_CHECK_INT(behavior_bt_reset(NULL), -1);
    tf_end();
}
