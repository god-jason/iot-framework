#ifndef ACTION_H
#define ACTION_H

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
    ACTION_IDLE,
    ACTION_RUNNING,
    ACTION_SUCCESS,
    ACTION_FAILED,
    ACTION_CANCELLED
} action_state_t;

typedef struct action action_t;

struct action {
    const char *name;

    int (*start)(action_t *action);
    int (*update)(action_t *action);
    int (*cancel)(action_t *action);
    int (*reset)(action_t *action);

    action_state_t state;

    void *ctx;
};

int action_init(action_t *action);
int action_start(action_t *action);
int action_update(action_t *action);
int action_cancel(action_t *action);
int action_reset(action_t *action);

action_state_t action_get_state(const action_t *action);

#ifdef __cplusplus
}
#endif

#endif /* ACTION_H */