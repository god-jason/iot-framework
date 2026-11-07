#ifndef ROBOT_ACTION_H
#define ROBOT_ACTION_H

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
    ROBOT_ACTION_IDLE,
    ROBOT_ACTION_RUNNING,
    ROBOT_ACTION_SUCCESS,
    ROBOT_ACTION_FAILED,
    ROBOT_ACTION_CANCELLED
} robot_action_state_t;

typedef struct robot_action robot_action_t;

struct robot_action {
    const char *name;

    int (*start)(robot_action_t *action);
    int (*update)(robot_action_t *action);
    int (*cancel)(robot_action_t *action);
    int (*reset)(robot_action_t *action);

    robot_action_state_t state;

    void *ctx;
};

int robot_action_init(robot_action_t *action);
int robot_action_start(robot_action_t *action);
int robot_action_update(robot_action_t *action);
int robot_action_cancel(robot_action_t *action);
int robot_action_reset(robot_action_t *action);

robot_action_state_t robot_action_get_state(const robot_action_t *action);

#ifdef __cplusplus
}
#endif

#endif /* ROBOT_ACTION_H */