#include "robot_bt.h"
#include "iot_memory.h"

#define ROBOT_BT_MAX_NODES 32

static robot_bt_node_t s_nodes[ROBOT_BT_MAX_NODES];
static bool s_used[ROBOT_BT_MAX_NODES];
static uint32_t s_bt_tick = 0;

static robot_bt_node_t *bt_alloc(void)
{
    int i;
    for (i = 0; i < ROBOT_BT_MAX_NODES; i++) {
        if (!s_used[i]) {
            s_used[i] = true;
            iot_memory_zero(&s_nodes[i], sizeof(s_nodes[i]));
            s_nodes[i].counter = -1;
            return &s_nodes[i];
        }
    }
    return NULL;
}

static robot_bt_status_t bt_tick_sequence(robot_bt_node_t *n);
static robot_bt_status_t bt_tick_selector(robot_bt_node_t *n);
static robot_bt_status_t bt_tick_parallel(robot_bt_node_t *n);
static robot_bt_status_t bt_tick_inverter(robot_bt_node_t *n);
static robot_bt_status_t bt_tick_repeat(robot_bt_node_t *n);
static robot_bt_status_t bt_tick_delay(robot_bt_node_t *n);
static robot_bt_status_t bt_tick_action(robot_bt_node_t *n);
static robot_bt_status_t bt_tick_condition(robot_bt_node_t *n);

static void bt_halt_rec(robot_bt_node_t *n)
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

static void bt_reset_rec(robot_bt_node_t *n)
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

int robot_bt_init(robot_bt_node_t *root)
{
    s_bt_tick = 0;
    if (root == NULL) {
        return -1;
    }
    bt_reset_rec(root);
    return IOT_OK;
}

robot_bt_status_t robot_bt_tick(robot_bt_node_t *root)
{
    if (root == NULL) {
        return ROBOT_BT_FAILURE;
    }
    s_bt_tick++;
    return root->tick(root);
}

int robot_bt_halt(robot_bt_node_t *root)
{
    if (root == NULL) {
        return -1;
    }
    bt_halt_rec(root);
    return IOT_OK;
}

int robot_bt_reset(robot_bt_node_t *root)
{
    if (root == NULL) {
        return -1;
    }
    bt_reset_rec(root);
    return IOT_OK;
}

robot_bt_node_t *robot_bt_sequence_create(robot_bt_node_t **children, uint16_t count)
{
    robot_bt_node_t *n = bt_alloc();
    if (n == NULL) {
        return NULL;
    }
    n->type = ROBOT_BT_SEQUENCE;
    n->tick = bt_tick_sequence;
    n->children = children;
    n->child_count = count;
    return n;
}

robot_bt_node_t *robot_bt_selector_create(robot_bt_node_t **children, uint16_t count)
{
    robot_bt_node_t *n = bt_alloc();
    if (n == NULL) {
        return NULL;
    }
    n->type = ROBOT_BT_SELECTOR;
    n->tick = bt_tick_selector;
    n->children = children;
    n->child_count = count;
    return n;
}

robot_bt_node_t *robot_bt_parallel_create(robot_bt_node_t **children, uint16_t count)
{
    robot_bt_node_t *n = bt_alloc();
    if (n == NULL) {
        return NULL;
    }
    n->type = ROBOT_BT_PARALLEL;
    n->tick = bt_tick_parallel;
    n->children = children;
    n->child_count = count;
    return n;
}

robot_bt_node_t *robot_bt_inverter_create(robot_bt_node_t **children, uint16_t count)
{
    robot_bt_node_t *n = bt_alloc();
    if (n == NULL) {
        return NULL;
    }
    n->type = ROBOT_BT_INVERTER;
    n->tick = bt_tick_inverter;
    n->children = children;
    n->child_count = count;
    return n;
}

robot_bt_node_t *robot_bt_repeat_create(robot_bt_node_t **children, uint16_t count, uint16_t times)
{
    robot_bt_node_t *n = bt_alloc();
    if (n == NULL) {
        return NULL;
    }
    n->type = ROBOT_BT_REPEAT;
    n->tick = bt_tick_repeat;
    n->children = children;
    n->child_count = count;
    n->config = times;
    return n;
}

robot_bt_node_t *robot_bt_delay_create(robot_bt_node_t **children, uint16_t count, uint32_t delay_ticks)
{
    robot_bt_node_t *n = bt_alloc();
    if (n == NULL) {
        return NULL;
    }
    n->type = ROBOT_BT_DELAY;
    n->tick = bt_tick_delay;
    n->children = children;
    n->child_count = count;
    n->config = delay_ticks;
    return n;
}

robot_bt_node_t *robot_bt_action_create(robot_bt_status_t (*fn)(robot_bt_node_t *node), void *ctx)
{
    robot_bt_node_t *n = bt_alloc();
    if (n == NULL) {
        return NULL;
    }
    n->type = ROBOT_BT_ACTION;
    n->tick = bt_tick_action;
    n->action_fn = fn;
    n->ctx = ctx;
    return n;
}

robot_bt_node_t *robot_bt_condition_create(bool (*fn)(robot_bt_node_t *node), void *ctx)
{
    robot_bt_node_t *n = bt_alloc();
    if (n == NULL) {
        return NULL;
    }
    n->type = ROBOT_BT_CONDITION;
    n->tick = bt_tick_condition;
    n->cond_fn = fn;
    n->ctx = ctx;
    return n;
}

static robot_bt_status_t bt_tick_sequence(robot_bt_node_t *n)
{
    uint16_t i;
    for (i = 0; i < n->child_count; i++) {
        robot_bt_node_t *c = n->children[i];
        robot_bt_status_t s = c->tick(c);
        if (s == ROBOT_BT_FAILURE) {
            return ROBOT_BT_FAILURE;
        }
        if (s == ROBOT_BT_RUNNING) {
            return ROBOT_BT_RUNNING;
        }
    }
    return ROBOT_BT_SUCCESS;
}

static robot_bt_status_t bt_tick_selector(robot_bt_node_t *n)
{
    uint16_t i;
    for (i = 0; i < n->child_count; i++) {
        robot_bt_node_t *c = n->children[i];
        robot_bt_status_t s = c->tick(c);
        if (s == ROBOT_BT_SUCCESS) {
            return ROBOT_BT_SUCCESS;
        }
        if (s == ROBOT_BT_RUNNING) {
            return ROBOT_BT_RUNNING;
        }
    }
    return ROBOT_BT_FAILURE;
}

static robot_bt_status_t bt_tick_parallel(robot_bt_node_t *n)
{
    uint16_t i;
    bool any_fail = false;
    bool all_success = true;

    for (i = 0; i < n->child_count; i++) {
        robot_bt_status_t s = n->children[i]->tick(n->children[i]);
        if (s == ROBOT_BT_FAILURE) {
            any_fail = true;
        }
        if (s != ROBOT_BT_SUCCESS) {
            all_success = false;
        }
    }
    if (any_fail) {
        return ROBOT_BT_FAILURE;
    }
    if (all_success) {
        return ROBOT_BT_SUCCESS;
    }
    return ROBOT_BT_RUNNING;
}

static robot_bt_status_t bt_tick_inverter(robot_bt_node_t *n)
{
    robot_bt_node_t *c = (n->child_count > 0) ? n->children[0] : NULL;
    robot_bt_status_t s;

    if (c == NULL) {
        return ROBOT_BT_FAILURE;
    }
    s = c->tick(c);
    if (s == ROBOT_BT_SUCCESS) {
        return ROBOT_BT_FAILURE;
    }
    if (s == ROBOT_BT_FAILURE) {
        return ROBOT_BT_SUCCESS;
    }
    return ROBOT_BT_RUNNING;
}

static robot_bt_status_t bt_tick_repeat(robot_bt_node_t *n)
{
    robot_bt_node_t *c = (n->child_count > 0) ? n->children[0] : NULL;

    if (c == NULL) {
        return ROBOT_BT_FAILURE;
    }
    if (n->counter < 0) {
        n->counter = (int32_t)n->config;
    }
    while (n->counter > 0) {
        robot_bt_status_t s = c->tick(c);
        if (s == ROBOT_BT_FAILURE) {
            n->counter = -1;
            return ROBOT_BT_FAILURE;
        }
        if (s == ROBOT_BT_RUNNING) {
            return ROBOT_BT_RUNNING;
        }
        n->counter--;
        if (n->counter > 0 && c->reset != NULL) {
            c->reset(c);
        }
    }
    n->counter = -1;
    return ROBOT_BT_SUCCESS;
}

static robot_bt_status_t bt_tick_delay(robot_bt_node_t *n)
{
    robot_bt_node_t *c = (n->child_count > 0) ? n->children[0] : NULL;

    if (c == NULL) {
        return ROBOT_BT_FAILURE;
    }
    if (n->counter < 0) {
        n->counter = (int32_t)s_bt_tick;
    }
    if ((s_bt_tick - (uint32_t)n->counter) < n->config) {
        return ROBOT_BT_RUNNING;
    }
    return c->tick(c);
}

static robot_bt_status_t bt_tick_action(robot_bt_node_t *n)
{
    return (n->action_fn != NULL) ? n->action_fn(n) : ROBOT_BT_FAILURE;
}

static robot_bt_status_t bt_tick_condition(robot_bt_node_t *n)
{
    bool ok = (n->cond_fn != NULL) ? n->cond_fn(n) : false;
    return ok ? ROBOT_BT_SUCCESS : ROBOT_BT_FAILURE;
}