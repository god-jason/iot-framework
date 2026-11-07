#ifndef ROBOT_BT_H
#define ROBOT_BT_H

/*
 * 行为树（Behavior Tree）。
 * 组合节点（sequence/selector/parallel/inverter/repeat/delay）每次 tick 重新从头评估子节点；
 * repeat / delay 通过节点内部 state 保持进度。
 * 节点由内部静态节点池分配，无动态内存。
 */

#include "iot_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ROBOT_BT_SUCCESS,
    ROBOT_BT_FAILURE,
    ROBOT_BT_RUNNING
} robot_bt_status_t;

typedef enum {
    ROBOT_BT_SEQUENCE,
    ROBOT_BT_SELECTOR,
    ROBOT_BT_PARALLEL,
    ROBOT_BT_INVERTER,
    ROBOT_BT_REPEAT,
    ROBOT_BT_ACTION,
    ROBOT_BT_CONDITION,
    ROBOT_BT_DELAY
} robot_bt_type_t;

typedef struct robot_bt_node robot_bt_node_t;

struct robot_bt_node {
    robot_bt_type_t type;

    robot_bt_status_t (*tick)(robot_bt_node_t *node);
    void (*halt)(robot_bt_node_t *node);
    void (*reset)(robot_bt_node_t *node);

    robot_bt_node_t **children;
    uint16_t child_count;

    void *ctx;

    /* ---- 以下为框架内部字段 ---- */
    robot_bt_status_t (*action_fn)(robot_bt_node_t *node); /* action 行为 */
    bool (*cond_fn)(robot_bt_node_t *node);              /* condition 判断 */

    int32_t  counter;  /* repeat 剩余次数 / delay 起始 tick */
    uint32_t config;   /* repeat 次数 / delay ticks */
};

int robot_bt_init(robot_bt_node_t *root);
robot_bt_status_t robot_bt_tick(robot_bt_node_t *root);
int robot_bt_halt(robot_bt_node_t *root);
int robot_bt_reset(robot_bt_node_t *root);

robot_bt_node_t *robot_bt_sequence_create(robot_bt_node_t **children, uint16_t count);
robot_bt_node_t *robot_bt_selector_create(robot_bt_node_t **children, uint16_t count);
robot_bt_node_t *robot_bt_parallel_create(robot_bt_node_t **children, uint16_t count);
robot_bt_node_t *robot_bt_inverter_create(robot_bt_node_t **children, uint16_t count);
robot_bt_node_t *robot_bt_repeat_create(robot_bt_node_t **children, uint16_t count, uint16_t times);
robot_bt_node_t *robot_bt_delay_create(robot_bt_node_t **children, uint16_t count, uint32_t delay_ticks);

robot_bt_node_t *robot_bt_action_create(robot_bt_status_t (*fn)(robot_bt_node_t *node), void *ctx);
robot_bt_node_t *robot_bt_condition_create(bool (*fn)(robot_bt_node_t *node), void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* ROBOT_BT_H */