/*
 * behavior_test.c - 行为树测试（对应 behavior_bt.c）
 *
 * 覆盖：动作/条件叶子、顺序、选择、并行、取反、重复、延时、
 *       复位与停止、NULL 安全。
 */

#include "test.h"
#include "behavior_bt.h"

static int s_sequence[16];
static int s_seq_len;
static int s_action_value;

static int action_fn(behavior_bt_node_t *node)
{
    (void)node;
    if (s_seq_len < (int)(sizeof(s_sequence) / sizeof(s_sequence[0]))) {
        s_sequence[s_seq_len++] = s_action_value;
    }
    return s_action_value;
}

static bool cond_true(behavior_bt_node_t *node)
{
    (void)node;
    return true;
}

static bool cond_false(behavior_bt_node_t *node)
{
    (void)node;
    return false;
}

void ut_robot_bt(void)
{
    tf_begin("bt/叶子节点");

    {
        behavior_bt_node_t *act = behavior_bt_action_create(action_fn, NULL);
        behavior_bt_node_t *t = behavior_bt_condition_create(cond_true, NULL);
        behavior_bt_node_t *f = behavior_bt_condition_create(cond_false, NULL);

        TF_CHECK(act != NULL);
        TF_CHECK(t != NULL);
        TF_CHECK(f != NULL);

        if (act != NULL && t != NULL && f != NULL) {
            TF_CHECK_INT(behavior_bt_init(NULL), -1);
            TF_CHECK_INT(behavior_bt_init(act), 0);

            s_action_value = BEHAVIOR_BT_SUCCESS;
            TF_CHECK_INT(behavior_bt_tick(act), BEHAVIOR_BT_SUCCESS);

            s_action_value = BEHAVIOR_BT_FAILURE;
            TF_CHECK_INT(behavior_bt_tick(act), BEHAVIOR_BT_FAILURE);

            s_action_value = BEHAVIOR_BT_RUNNING;
            TF_CHECK_INT(behavior_bt_tick(act), BEHAVIOR_BT_RUNNING);

            TF_CHECK_INT(behavior_bt_tick(t), BEHAVIOR_BT_SUCCESS);
            TF_CHECK_INT(behavior_bt_tick(f), BEHAVIOR_BT_FAILURE);

            /* 无 tick 实现的 NULL 根节点 */
            TF_CHECK_INT(behavior_bt_tick(NULL), BEHAVIOR_BT_FAILURE);
        }
    }

    tf_end();

    tf_begin("bt/顺序与选择");

    {
        behavior_bt_node_t *children[2];
        behavior_bt_node_t *seq;

        children[0] = behavior_bt_action_create(action_fn, NULL);
        children[1] = behavior_bt_action_create(action_fn, NULL);
        TF_CHECK(children[0] != NULL && children[1] != NULL);

        if (children[0] != NULL && children[1] != NULL) {
            /* 顺序：全部成功才成功 */
            seq = behavior_bt_sequence_create(children, 2U);
            TF_CHECK(seq != NULL);
            if (seq != NULL) {
                TF_CHECK_INT(behavior_bt_init(seq), 0);
                s_action_value = BEHAVIOR_BT_SUCCESS;
                s_seq_len = 0;
                TF_CHECK_INT(behavior_bt_tick(seq), BEHAVIOR_BT_SUCCESS);
                TF_CHECK_INT(s_seq_len, 2);

                /* 第一个失败即短路 */
                s_seq_len = 0;
                s_action_value = BEHAVIOR_BT_FAILURE;
                TF_CHECK_INT(behavior_bt_tick(seq), BEHAVIOR_BT_FAILURE);
                TF_CHECK_INT(s_seq_len, 1);
            }
        }
    }

    {
        behavior_bt_node_t *children[2];
        behavior_bt_node_t *sel;

        children[0] = behavior_bt_condition_create(cond_false, NULL);
        children[1] = behavior_bt_condition_create(cond_true, NULL);
        TF_CHECK(children[0] != NULL && children[1] != NULL);

        if (children[0] != NULL && children[1] != NULL) {
            /* 选择：任一成功即成功 */
            sel = behavior_bt_selector_create(children, 2U);
            TF_CHECK(sel != NULL);
            if (sel != NULL) {
                TF_CHECK_INT(behavior_bt_init(sel), 0);
                TF_CHECK_INT(behavior_bt_tick(sel), BEHAVIOR_BT_SUCCESS);
            }
        }
    }

    tf_end();

    tf_begin("bt/取反与重复");

    {
        behavior_bt_node_t *children[1];
        behavior_bt_node_t *inv;

        children[0] = behavior_bt_condition_create(cond_false, NULL);
        TF_CHECK(children[0] != NULL);
        if (children[0] != NULL) {
            inv = behavior_bt_inverter_create(children, 1U);
            TF_CHECK(inv != NULL);
            if (inv != NULL) {
                TF_CHECK_INT(behavior_bt_init(inv), 0);
                TF_CHECK_INT(behavior_bt_tick(inv), BEHAVIOR_BT_SUCCESS);
            }
        }
    }

    {
        behavior_bt_node_t *children[1];
        behavior_bt_node_t *rep;

        children[0] = behavior_bt_action_create(action_fn, NULL);
        TF_CHECK(children[0] != NULL);
        if (children[0] != NULL) {
            rep = behavior_bt_repeat_create(children, 1U, 3U);
            TF_CHECK(rep != NULL);
            if (rep != NULL) {
                TF_CHECK_INT(behavior_bt_init(rep), 0);
                s_action_value = BEHAVIOR_BT_SUCCESS;
                s_seq_len = 0;
                TF_CHECK_INT(behavior_bt_tick(rep), BEHAVIOR_BT_SUCCESS);
                TF_CHECK_INT(s_seq_len, 3);
            }
        }
    }

    tf_end();

    tf_begin("bt/延时与管理接口");

    {
        behavior_bt_node_t *children[1];
        behavior_bt_node_t *delay;

        children[0] = behavior_bt_action_create(action_fn, NULL);
        TF_CHECK(children[0] != NULL);
        if (children[0] != NULL) {
            delay = behavior_bt_delay_create(children, 1U, 2U);
            TF_CHECK(delay != NULL);
            if (delay != NULL) {
                TF_CHECK_INT(behavior_bt_init(delay), 0);
                s_action_value = BEHAVIOR_BT_SUCCESS;
                s_seq_len = 0;

                TF_CHECK_INT(behavior_bt_tick(delay), BEHAVIOR_BT_RUNNING);
                TF_CHECK_INT(behavior_bt_tick(delay), BEHAVIOR_BT_RUNNING);
                TF_CHECK_INT(behavior_bt_tick(delay), BEHAVIOR_BT_SUCCESS);
                TF_CHECK_INT(s_seq_len, 1);

                TF_CHECK_INT(behavior_bt_halt(delay), 0);
                TF_CHECK_INT(behavior_bt_reset(delay), 0);
                TF_CHECK_INT(behavior_bt_halt(NULL), -1);
                TF_CHECK_INT(behavior_bt_reset(NULL), -1);
            }
        }
    }

    tf_end();
}
