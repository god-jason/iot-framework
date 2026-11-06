#ifndef IOT_FSM_H
#define IOT_FSM_H

/*
 * 有限状态机（无内置事件队列）。
 * 事件由调用者直接传入 iot_fsm_dispatch()，状态机只负责状态与转换逻辑。
 */

#include "iot_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct iot_fsm iot_fsm_t;
typedef struct iot_fsm_state iot_fsm_state_t;

typedef void (*iot_fsm_entry_fn)(iot_fsm_t *fsm, void *ctx);
typedef void (*iot_fsm_action_fn)(iot_fsm_t *fsm, void *ctx,
                                  uint32_t event, void *data);
typedef void (*iot_fsm_exit_fn)(iot_fsm_t *fsm, void *ctx);
typedef bool (*iot_fsm_guard_fn)(iot_fsm_t *fsm, void *ctx,
                                 uint32_t event, void *data);

struct iot_fsm_state {
    uint32_t id;

    iot_fsm_entry_fn entry;
    iot_fsm_action_fn action;
    iot_fsm_exit_fn exit;

    void *ctx;
};

typedef struct {
    uint32_t from;
    uint32_t event;
    uint32_t to;

    iot_fsm_guard_fn guard;
} iot_fsm_transition_t;

struct iot_fsm {
    const iot_fsm_state_t *states;
    uint16_t state_count;

    const iot_fsm_transition_t *transitions;
    uint16_t transition_count;

    const iot_fsm_state_t *current;

    void *ctx;

    bool running;
    bool paused;
};

int iot_fsm_init(iot_fsm_t *fsm,
                 const iot_fsm_state_t *states, uint16_t state_count,
                 const iot_fsm_transition_t *transitions, uint16_t transition_count,
                 void *ctx);

int iot_fsm_start(iot_fsm_t *fsm, uint32_t initial_state);
int iot_fsm_stop(iot_fsm_t *fsm);

int iot_fsm_pause(iot_fsm_t *fsm);
int iot_fsm_resume(iot_fsm_t *fsm);

int iot_fsm_dispatch(iot_fsm_t *fsm, uint32_t event, void *data);
int iot_fsm_transition(iot_fsm_t *fsm, uint32_t state_id);

uint32_t iot_fsm_get_state(const iot_fsm_t *fsm);
bool iot_fsm_is_state(const iot_fsm_t *fsm, uint32_t state_id);
bool iot_fsm_is_running(const iot_fsm_t *fsm);

#ifdef __cplusplus
}
#endif

#endif /* IOT_FSM_H */