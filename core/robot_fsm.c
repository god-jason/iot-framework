#include "robot_fsm.h"

static const robot_fsm_state_t *fsm_find_state(const robot_fsm_t *fsm, uint32_t id)
{
    uint16_t i;
    for (i = 0; i < fsm->state_count; i++) {
        if (fsm->states[i].id == id) {
            return &fsm->states[i];
        }
    }
    return NULL;
}

int robot_fsm_init(robot_fsm_t *fsm,
                 const robot_fsm_state_t *states, uint16_t state_count,
                 const robot_fsm_transition_t *transitions, uint16_t transition_count,
                 void *ctx)
{
    if (fsm == NULL || states == NULL || state_count == 0) {
        return -1;
    }
    fsm->states = states;
    fsm->state_count = state_count;
    fsm->transitions = transitions;
    fsm->transition_count = transition_count;
    fsm->current = NULL;
    fsm->ctx = ctx;
    fsm->running = false;
    fsm->paused = false;
    return IOT_OK;
}

int robot_fsm_start(robot_fsm_t *fsm, uint32_t initial_state)
{
    const robot_fsm_state_t *s;
    if (fsm == NULL) {
        return -1;
    }
    s = fsm_find_state(fsm, initial_state);
    if (s == NULL) {
        return -1;
    }
    fsm->current = s;
    fsm->running = true;
    fsm->paused = false;
    if (s->entry != NULL) {
        s->entry(fsm, s->ctx);
    }
    return IOT_OK;
}

int robot_fsm_stop(robot_fsm_t *fsm)
{
    if (fsm == NULL) {
        return -1;
    }
    if (fsm->running && fsm->current != NULL && fsm->current->exit != NULL) {
        fsm->current->exit(fsm, fsm->current->ctx);
    }
    fsm->running = false;
    fsm->paused = false;
    return IOT_OK;
}

int robot_fsm_pause(robot_fsm_t *fsm)
{
    if (fsm == NULL) {
        return -1;
    }
    fsm->paused = true;
    return IOT_OK;
}

int robot_fsm_resume(robot_fsm_t *fsm)
{
    if (fsm == NULL) {
        return -1;
    }
    fsm->paused = false;
    return IOT_OK;
}

int robot_fsm_dispatch(robot_fsm_t *fsm, uint32_t event, void *data)
{
    const robot_fsm_state_t *cur;
    uint16_t i;

    if (fsm == NULL || fsm->current == NULL) {
        return -1;
    }
    if (!fsm->running || fsm->paused) {
        return -1;
    }

    cur = fsm->current;

    /* 1. 执行当前状态 action */
    if (cur->action != NULL) {
        cur->action(fsm, cur->ctx, event, data);
    }

    /* 2. 查找匹配的事件转换 */
    for (i = 0; i < fsm->transition_count; i++) {
        const robot_fsm_transition_t *t = &fsm->transitions[i];
        const robot_fsm_state_t *next;

        if (t->from != cur->id || t->event != event) {
            continue;
        }
        /* 3. guard 判断 */
        if (t->guard != NULL && !t->guard(fsm, cur->ctx, event, data)) {
            return IOT_OK;
        }

        next = fsm_find_state(fsm, t->to);
        if (next == NULL) {
            return -1;
        }

        /* 4. 当前状态 exit */
        if (cur->exit != NULL) {
            cur->exit(fsm, cur->ctx);
        }
        /* 5. 切换状态 */
        fsm->current = next;
        /* 6. 新状态 entry */
        if (next->entry != NULL) {
            next->entry(fsm, next->ctx);
        }
        return IOT_OK;
    }

    return IOT_OK;
}

int robot_fsm_transition(robot_fsm_t *fsm, uint32_t state_id)
{
    const robot_fsm_state_t *next;
    if (fsm == NULL || fsm->current == NULL) {
        return -1;
    }
    next = fsm_find_state(fsm, state_id);
    if (next == NULL) {
        return -1;
    }
    if (fsm->current->exit != NULL) {
        fsm->current->exit(fsm, fsm->current->ctx);
    }
    fsm->current = next;
    if (next->entry != NULL) {
        next->entry(fsm, next->ctx);
    }
    return IOT_OK;
}

uint32_t robot_fsm_get_state(const robot_fsm_t *fsm)
{
    if (fsm == NULL || fsm->current == NULL) {
        return 0;
    }
    return fsm->current->id;
}

bool robot_fsm_is_state(const robot_fsm_t *fsm, uint32_t state_id)
{
    return (fsm != NULL && fsm->current != NULL && fsm->current->id == state_id);
}

bool robot_fsm_is_running(const robot_fsm_t *fsm)
{
    return (fsm != NULL) && fsm->running;
}