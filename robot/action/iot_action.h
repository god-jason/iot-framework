#ifndef IOT_ACTION_H
#define IOT_ACTION_H

/*
 * 动作（Action）：具有执行过程的任务（如投料、移动、充电）。
 * update() 回调约定返回值：
 *   0  -> 继续运行（RUNNING）
 *   >0 -> 完成（SUCCESS）
 *   <0 -> 失败（FAILED）
 */

#include "iot_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    IOT_ACTION_IDLE,
    IOT_ACTION_RUNNING,
    IOT_ACTION_SUCCESS,
    IOT_ACTION_FAILED,
    IOT_ACTION_CANCELLED
} iot_action_state_t;

typedef struct iot_action iot_action_t;

struct iot_action {
    const char *name;

    int (*start)(iot_action_t *action);
    int (*update)(iot_action_t *action);
    int (*cancel)(iot_action_t *action);
    int (*reset)(iot_action_t *action);

    iot_action_state_t state;

    void *ctx;
};

int iot_action_init(iot_action_t *action);
int iot_action_start(iot_action_t *action);
int iot_action_update(iot_action_t *action);
int iot_action_cancel(iot_action_t *action);
int iot_action_reset(iot_action_t *action);

iot_action_state_t iot_action_get_state(const iot_action_t *action);

#ifdef __cplusplus
}
#endif

#endif /* IOT_ACTION_H */