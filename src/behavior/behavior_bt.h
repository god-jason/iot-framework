#ifndef BEHAVIOR_BT_H
#define BEHAVIOR_BT_H

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
    BEHAVIOR_BT_SUCCESS,
    BEHAVIOR_BT_FAILURE,
    BEHAVIOR_BT_RUNNING
} behavior_bt_status_t;

typedef enum {
    BEHAVIOR_BT_SEQUENCE,
    BEHAVIOR_BT_SELECTOR,
    BEHAVIOR_BT_PARALLEL,
    BEHAVIOR_BT_INVERTER,
    BEHAVIOR_BT_REPEAT,
    BEHAVIOR_BT_ACTION,
    BEHAVIOR_BT_CONDITION,
    BEHAVIOR_BT_DELAY
} behavior_bt_type_t;

typedef struct behavior_bt_node behavior_bt_node_t;

struct behavior_bt_node {
    behavior_bt_type_t type;

    behavior_bt_status_t (*tick)(behavior_bt_node_t *node);
    void (*halt)(behavior_bt_node_t *node);
    void (*reset)(behavior_bt_node_t *node);

    behavior_bt_node_t **children;
    uint16_t child_count;

    void *ctx;

    /* ---- 以下为框架内部字段 ---- */
    behavior_bt_status_t (*action_fn)(behavior_bt_node_t *node); /* action 行为 */
    bool (*cond_fn)(behavior_bt_node_t *node);              /* condition 判断 */

    int32_t  counter;  /* repeat 剩余次数 / delay 起始 tick */
    uint32_t config;   /* repeat 次数 / delay ticks */
};

int behavior_bt_init(behavior_bt_node_t *root);
behavior_bt_status_t behavior_bt_tick(behavior_bt_node_t *root);
int behavior_bt_halt(behavior_bt_node_t *root);
int behavior_bt_reset(behavior_bt_node_t *root);

behavior_bt_node_t *behavior_bt_sequence_create(behavior_bt_node_t **children, uint16_t count);
behavior_bt_node_t *behavior_bt_selector_create(behavior_bt_node_t **children, uint16_t count);
behavior_bt_node_t *behavior_bt_parallel_create(behavior_bt_node_t **children, uint16_t count);
behavior_bt_node_t *behavior_bt_inverter_create(behavior_bt_node_t **children, uint16_t count);
behavior_bt_node_t *behavior_bt_repeat_create(behavior_bt_node_t **children, uint16_t count, uint16_t times);
behavior_bt_node_t *behavior_bt_delay_create(behavior_bt_node_t **children, uint16_t count, uint32_t delay_ticks);

behavior_bt_node_t *behavior_bt_action_create(behavior_bt_status_t (*fn)(behavior_bt_node_t *node), void *ctx);
behavior_bt_node_t *behavior_bt_condition_create(bool (*fn)(behavior_bt_node_t *node), void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* BEHAVIOR_BT_H */