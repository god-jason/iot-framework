#ifndef IOT_BT_H
#define IOT_BT_H

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
    IOT_BT_SUCCESS,
    IOT_BT_FAILURE,
    IOT_BT_RUNNING
} iot_bt_status_t;

typedef enum {
    IOT_BT_SEQUENCE,
    IOT_BT_SELECTOR,
    IOT_BT_PARALLEL,
    IOT_BT_INVERTER,
    IOT_BT_REPEAT,
    IOT_BT_ACTION,
    IOT_BT_CONDITION,
    IOT_BT_DELAY
} iot_bt_type_t;

typedef struct iot_bt_node iot_bt_node_t;

struct iot_bt_node {
    iot_bt_type_t type;

    iot_bt_status_t (*tick)(iot_bt_node_t *node);
    void (*halt)(iot_bt_node_t *node);
    void (*reset)(iot_bt_node_t *node);

    iot_bt_node_t **children;
    uint16_t child_count;

    void *ctx;

    /* ---- 以下为框架内部字段 ---- */
    iot_bt_status_t (*action_fn)(iot_bt_node_t *node); /* action 行为 */
    bool (*cond_fn)(iot_bt_node_t *node);              /* condition 判断 */

    int32_t  counter;  /* repeat 剩余次数 / delay 起始 tick */
    uint32_t config;   /* repeat 次数 / delay ticks */
};

int iot_bt_init(iot_bt_node_t *root);
iot_bt_status_t iot_bt_tick(iot_bt_node_t *root);
int iot_bt_halt(iot_bt_node_t *root);
int iot_bt_reset(iot_bt_node_t *root);

iot_bt_node_t *iot_bt_sequence_create(iot_bt_node_t **children, uint16_t count);
iot_bt_node_t *iot_bt_selector_create(iot_bt_node_t **children, uint16_t count);
iot_bt_node_t *iot_bt_parallel_create(iot_bt_node_t **children, uint16_t count);
iot_bt_node_t *iot_bt_inverter_create(iot_bt_node_t **children, uint16_t count);
iot_bt_node_t *iot_bt_repeat_create(iot_bt_node_t **children, uint16_t count, uint16_t times);
iot_bt_node_t *iot_bt_delay_create(iot_bt_node_t **children, uint16_t count, uint32_t delay_ticks);

iot_bt_node_t *iot_bt_action_create(iot_bt_status_t (*fn)(iot_bt_node_t *node), void *ctx);
iot_bt_node_t *iot_bt_condition_create(bool (*fn)(iot_bt_node_t *node), void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* IOT_BT_H */