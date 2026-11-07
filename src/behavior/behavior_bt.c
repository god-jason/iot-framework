#include "behavior_bt.h"
#include <string.h>

#define BEHAVIOR_BT_MAX_NODES 32

static behavior_bt_node_t s_nodes[BEHAVIOR_BT_MAX_NODES];
static bool s_used[BEHAVIOR_BT_MAX_NODES];
static uint32_t s_bt_tick = 0;

static behavior_bt_node_t *bt_alloc(void)
{
    int i;
    for (i = 0; i < BEHAVIOR_BT_MAX_NODES; i++) {
        if (!s_used[i]) {
            s_used[i] = true;
            memset(&s_nodes[i], 0, sizeof(s_nodes[i]));
            s_nodes[i].counter = -1;
            return &s_nodes[i];
        }
    }
    return NULL;
}

static behavior_bt_status_t bt_tick_sequence(behavior_bt_node_t *n);
static behavior_bt_status_t bt_tick_selector(behavior_bt_node_t *n);
static behavior_bt_status_t bt_tick_parallel(behavior_bt_node_t *n);
static behavior_bt_status_t bt_tick_inverter(behavior_bt_node_t *n);
static behavior_bt_status_t bt_tick_repeat(behavior_bt_node_t *n);
static behavior_bt_status_t bt_tick_delay(behavior_bt_node_t *n);
static behavior_bt_status_t bt_tick_action(behavior_bt_node_t *n);
static behavior_bt_status_t bt_tick_condition(behavior_bt_node_t *n);

static void bt_halt_rec(behavior_bt_node_t *n)
{
    uint16_t i;
    if (n == NULL) {
        return;
    }
    for (i = 0; i < n->child_count; i++) {
        if (n->children != NULL && n->children[i] != NULL) {
            bt_halt_rec(n->children[i]);
        }
    }
    if (n->halt != NULL) {
        n->halt(n);
    }
}

static void bt_reset_rec(behavior_bt_node_t *n)
{
    uint16_t i;
    if (n == NULL) {
        return;
    }
    n->counter = -1;
    for (i = 0; i < n->child_count; i++) {
        if (n->children != NULL && n->children[i] != NULL) {
            bt_reset_rec(n->children[i]);
        }
    }
    if (n->reset != NULL) {
        n->reset(n);
    }
}

int behavior_bt_init(behavior_bt_node_t *root)
{
    s_bt_tick = 0;
    if (root == NULL) {
        return -1;
    }
    bt_reset_rec(root);
    return IOT_OK;
}

behavior_bt_status_t behavior_bt_tick(behavior_bt_node_t *root)
{
    if (root == NULL) {
        return BEHAVIOR_BT_FAILURE;
    }
    s_bt_tick++;
    return root->tick(root);
}

int behavior_bt_halt(behavior_bt_node_t *root)
{
    if (root == NULL) {
        return -1;
    }
    bt_halt_rec(root);
    return IOT_OK;
}

int behavior_bt_reset(behavior_bt_node_t *root)
{
    if (root == NULL) {
        return -1;
    }
    bt_reset_rec(root);
    return IOT_OK;
}

behavior_bt_node_t *behavior_bt_sequence_create(behavior_bt_node_t **children, uint16_t count)
{
    behavior_bt_node_t *n = bt_alloc();
    if (n == NULL) {
        return NULL;
    }
    n->type = BEHAVIOR_BT_SEQUENCE;
    n->tick = bt_tick_sequence;
    n->children = children;
    n->child_count = count;
    return n;
}

behavior_bt_node_t *behavior_bt_selector_create(behavior_bt_node_t **children, uint16_t count)
{
    behavior_bt_node_t *n = bt_alloc();
    if (n == NULL) {
        return NULL;
    }
    n->type = BEHAVIOR_BT_SELECTOR;
    n->tick = bt_tick_selector;
    n->children = children;
    n->child_count = count;
    return n;
}

behavior_bt_node_t *behavior_bt_parallel_create(behavior_bt_node_t **children, uint16_t count)
{
    behavior_bt_node_t *n = bt_alloc();
    if (n == NULL) {
        return NULL;
    }
    n->type = BEHAVIOR_BT_PARALLEL;
    n->tick = bt_tick_parallel;
    n->children = children;
    n->child_count = count;
    return n;
}

behavior_bt_node_t *behavior_bt_inverter_create(behavior_bt_node_t **children, uint16_t count)
{
    behavior_bt_node_t *n = bt_alloc();
    if (n == NULL) {
        return NULL;
    }
    n->type = BEHAVIOR_BT_INVERTER;
    n->tick = bt_tick_inverter;
    n->children = children;
    n->child_count = count;
    return n;
}

behavior_bt_node_t *behavior_bt_repeat_create(behavior_bt_node_t **children, uint16_t count, uint16_t times)
{
    behavior_bt_node_t *n = bt_alloc();
    if (n == NULL) {
        return NULL;
    }
    n->type = BEHAVIOR_BT_REPEAT;
    n->tick = bt_tick_repeat;
    n->children = children;
    n->child_count = count;
    n->config = times;
    return n;
}

behavior_bt_node_t *behavior_bt_delay_create(behavior_bt_node_t **children, uint16_t count, uint32_t delay_ticks)
{
    behavior_bt_node_t *n = bt_alloc();
    if (n == NULL) {
        return NULL;
    }
    n->type = BEHAVIOR_BT_DELAY;
    n->tick = bt_tick_delay;
    n->children = children;
    n->child_count = count;
    n->config = delay_ticks;
    return n;
}

behavior_bt_node_t *behavior_bt_action_create(behavior_bt_status_t (*fn)(behavior_bt_node_t *node), void *ctx)
{
    behavior_bt_node_t *n = bt_alloc();
    if (n == NULL) {
        return NULL;
    }
    n->type = BEHAVIOR_BT_ACTION;
    n->tick = bt_tick_action;
    n->action_fn = fn;
    n->ctx = ctx;
    return n;
}

behavior_bt_node_t *behavior_bt_condition_create(bool (*fn)(behavior_bt_node_t *node), void *ctx)
{
    behavior_bt_node_t *n = bt_alloc();
    if (n == NULL) {
        return NULL;
    }
    n->type = BEHAVIOR_BT_CONDITION;
    n->tick = bt_tick_condition;
    n->cond_fn = fn;
    n->ctx = ctx;
    return n;
}

static behavior_bt_status_t bt_tick_sequence(behavior_bt_node_t *n)
{
    uint16_t i;
    for (i = 0; i < n->child_count; i++) {
        behavior_bt_node_t *c = n->children[i];
        behavior_bt_status_t s = c->tick(c);
        if (s == BEHAVIOR_BT_FAILURE) {
            return BEHAVIOR_BT_FAILURE;
        }
        if (s == BEHAVIOR_BT_RUNNING) {
            return BEHAVIOR_BT_RUNNING;
        }
    }
    return BEHAVIOR_BT_SUCCESS;
}

static behavior_bt_status_t bt_tick_selector(behavior_bt_node_t *n)
{
    uint16_t i;
    for (i = 0; i < n->child_count; i++) {
        behavior_bt_node_t *c = n->children[i];
        behavior_bt_status_t s = c->tick(c);
        if (s == BEHAVIOR_BT_SUCCESS) {
            return BEHAVIOR_BT_SUCCESS;
        }
        if (s == BEHAVIOR_BT_RUNNING) {
            return BEHAVIOR_BT_RUNNING;
        }
    }
    return BEHAVIOR_BT_FAILURE;
}

static behavior_bt_status_t bt_tick_parallel(behavior_bt_node_t *n)
{
    uint16_t i;
    bool any_fail = false;
    bool all_success = true;

    for (i = 0; i < n->child_count; i++) {
        behavior_bt_status_t s = n->children[i]->tick(n->children[i]);
        if (s == BEHAVIOR_BT_FAILURE) {
            any_fail = true;
        }
        if (s != BEHAVIOR_BT_SUCCESS) {
            all_success = false;
        }
    }
    if (any_fail) {
        return BEHAVIOR_BT_FAILURE;
    }
    if (all_success) {
        return BEHAVIOR_BT_SUCCESS;
    }
    return BEHAVIOR_BT_RUNNING;
}

static behavior_bt_status_t bt_tick_inverter(behavior_bt_node_t *n)
{
    behavior_bt_node_t *c = (n->child_count > 0) ? n->children[0] : NULL;
    behavior_bt_status_t s;

    if (c == NULL) {
        return BEHAVIOR_BT_FAILURE;
    }
    s = c->tick(c);
    if (s == BEHAVIOR_BT_SUCCESS) {
        return BEHAVIOR_BT_FAILURE;
    }
    if (s == BEHAVIOR_BT_FAILURE) {
        return BEHAVIOR_BT_SUCCESS;
    }
    return BEHAVIOR_BT_RUNNING;
}

static behavior_bt_status_t bt_tick_repeat(behavior_bt_node_t *n)
{
    behavior_bt_node_t *c = (n->child_count > 0) ? n->children[0] : NULL;

    if (c == NULL) {
        return BEHAVIOR_BT_FAILURE;
    }
    if (n->counter < 0) {
        n->counter = (int32_t)n->config;
    }
    while (n->counter > 0) {
        behavior_bt_status_t s = c->tick(c);
        if (s == BEHAVIOR_BT_FAILURE) {
            n->counter = -1;
            return BEHAVIOR_BT_FAILURE;
        }
        if (s == BEHAVIOR_BT_RUNNING) {
            return BEHAVIOR_BT_RUNNING;
        }
        n->counter--;
        if (n->counter > 0 && c->reset != NULL) {
            c->reset(c);
        }
    }
    n->counter = -1;
    return BEHAVIOR_BT_SUCCESS;
}

static behavior_bt_status_t bt_tick_delay(behavior_bt_node_t *n)
{
    behavior_bt_node_t *c = (n->child_count > 0) ? n->children[0] : NULL;

    if (c == NULL) {
        return BEHAVIOR_BT_FAILURE;
    }
    if (n->counter < 0) {
        n->counter = (int32_t)s_bt_tick;
    }
    if ((s_bt_tick - (uint32_t)n->counter) < n->config) {
        return BEHAVIOR_BT_RUNNING;
    }
    return c->tick(c);
}

static behavior_bt_status_t bt_tick_action(behavior_bt_node_t *n)
{
    return (n->action_fn != NULL) ? n->action_fn(n) : BEHAVIOR_BT_FAILURE;
}

static behavior_bt_status_t bt_tick_condition(behavior_bt_node_t *n)
{
    bool ok = (n->cond_fn != NULL) ? n->cond_fn(n) : false;
    return ok ? BEHAVIOR_BT_SUCCESS : BEHAVIOR_BT_FAILURE;
}