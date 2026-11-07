#ifndef ROBOT_FSM_H
#define ROBOT_FSM_H

/*
 * 有限状态机（无内置事件队列）。
 * 事件由调用者直接传入 robot_fsm_dispatch()，状态机只负责状态与转换逻辑。
 */

#include "iot_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct robot_fsm robot_fsm_t;
typedef struct robot_fsm_state robot_fsm_state_t;

typedef void (*robot_fsm_entry_fn)(robot_fsm_t *fsm, void *ctx);
typedef void (*robot_fsm_action_fn)(robot_fsm_t *fsm, void *ctx,
                                  uint32_t event, void *data);
typedef void (*robot_fsm_exit_fn)(robot_fsm_t *fsm, void *ctx);
typedef bool (*robot_fsm_guard_fn)(robot_fsm_t *fsm, void *ctx,
                                 uint32_t event, void *data);

struct robot_fsm_state {
    uint32_t id;

    robot_fsm_entry_fn entry;
    robot_fsm_action_fn action;
    robot_fsm_exit_fn exit;

    void *ctx;
};

typedef struct {
    uint32_t from;
    uint32_t event;
    uint32_t to;

    robot_fsm_guard_fn guard;
} robot_fsm_transition_t;

struct robot_fsm {
    const robot_fsm_state_t *states;
    uint16_t state_count;

    const robot_fsm_transition_t *transitions;
    uint16_t transition_count;

    const robot_fsm_state_t *current;

    void *ctx;

    bool running;
    bool paused;
};

int robot_fsm_init(robot_fsm_t *fsm,
                 const robot_fsm_state_t *states, uint16_t state_count,
                 const robot_fsm_transition_t *transitions, uint16_t transition_count,
                 void *ctx);

int robot_fsm_start(robot_fsm_t *fsm, uint32_t initial_state);
int robot_fsm_stop(robot_fsm_t *fsm);

int robot_fsm_pause(robot_fsm_t *fsm);
int robot_fsm_resume(robot_fsm_t *fsm);

int robot_fsm_dispatch(robot_fsm_t *fsm, uint32_t event, void *data);
int robot_fsm_transition(robot_fsm_t *fsm, uint32_t state_id);

uint32_t robot_fsm_get_state(const robot_fsm_t *fsm);
bool robot_fsm_is_state(const robot_fsm_t *fsm, uint32_t state_id);
bool robot_fsm_is_running(const robot_fsm_t *fsm);

#ifdef __cplusplus
}
#endif

#endif /* ROBOT_FSM_H */