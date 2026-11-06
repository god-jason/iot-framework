#include "iot_bt.h"
#include "iot_memory.h"

#define IOT_BT_MAX_NODES 32

static iot_bt_node_t s_nodes[IOT_BT_MAX_NODES];
static bool s_used[IOT_BT_MAX_NODES];
static uint32_t s_bt_tick = 0;

static iot_bt_node_t *bt_alloc(void)
{
    int i;
    for (i = 0; i < IOT_BT_MAX_NODES; i++) {
        if (!s_used[i]) {
            s_used[i] = true;
            iot_memory_zero(&s_nodes[i], sizeof(s_nodes[i]));
            s_nodes[i].counter = -1;
            return &s_nodes[i];
        }
    }
    return NULL;
}

static iot_bt_status_t bt_tick_sequence(iot_bt_node_t *n);
static iot_bt_status_t bt_tick_selector(iot_bt_node_t *n);
static iot_bt_status_t bt_tick_parallel(iot_bt_node_t *n);
static iot_bt_status_t bt_tick_inverter(iot_bt_node_t *n);
static iot_bt_status_t bt_tick_repeat(iot_bt_node_t *n);
static iot_bt_status_t bt_tick_delay(iot_bt_node_t *n);
static iot_bt_status_t bt_tick_action(iot_bt_node_t *n);
static iot_bt_status_t bt_tick_condition(iot_bt_node_t *n);

static void bt_halt_rec(iot_bt_node_t *n)
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

static void bt_reset_rec(iot_bt_node_t *n)
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

int iot_bt_init(iot_bt_node_t *root)
{
    s_bt_tick = 0;
    if (root == NULL) {
        return IOT_ERR_PARAM;
    }
    bt_reset_rec(root);
    return IOT_OK;
}

iot_bt_status_t iot_bt_tick(iot_bt_node_t *root)
{
    if (root == NULL) {
        return IOT_BT_FAILURE;
    }
    s_bt_tick++;
    return root->tick(root);
}

int iot_bt_halt(iot_bt_node_t *root)
{
    if (root == NULL) {
        return IOT_ERR_PARAM;
    }
    bt_halt_rec(root);
    return IOT_OK;
}

int iot_bt_reset(iot_bt_node_t *root)
{
    if (root == NULL) {
        return IOT_ERR_PARAM;
    }
    bt_reset_rec(root);
    return IOT_OK;
}

iot_bt_node_t *iot_bt_sequence_create(iot_bt_node_t **children, uint16_t count)
{
    iot_bt_node_t *n = bt_alloc();
    if (n == NULL) {
        return NULL;
    }
    n->type = IOT_BT_SEQUENCE;
    n->tick = bt_tick_sequence;
    n->children = children;
    n->child_count = count;
    return n;
}

iot_bt_node_t *iot_bt_selector_create(iot_bt_node_t **children, uint16_t count)
{
    iot_bt_node_t *n = bt_alloc();
    if (n == NULL) {
        return NULL;
    }
    n->type = IOT_BT_SELECTOR;
    n->tick = bt_tick_selector;
    n->children = children;
    n->child_count = count;
    return n;
}

iot_bt_node_t *iot_bt_parallel_create(iot_bt_node_t **children, uint16_t count)
{
    iot_bt_node_t *n = bt_alloc();
    if (n == NULL) {
        return NULL;
    }
    n->type = IOT_BT_PARALLEL;
    n->tick = bt_tick_parallel;
    n->children = children;
    n->child_count = count;
    return n;
}

iot_bt_node_t *iot_bt_inverter_create(iot_bt_node_t **children, uint16_t count)
{
    iot_bt_node_t *n = bt_alloc();
    if (n == NULL) {
        return NULL;
    }
    n->type = IOT_BT_INVERTER;
    n->tick = bt_tick_inverter;
    n->children = children;
    n->child_count = count;
    return n;
}

iot_bt_node_t *iot_bt_repeat_create(iot_bt_node_t **children, uint16_t count, uint16_t times)
{
    iot_bt_node_t *n = bt_alloc();
    if (n == NULL) {
        return NULL;
    }
    n->type = IOT_BT_REPEAT;
    n->tick = bt_tick_repeat;
    n->children = children;
    n->child_count = count;
    n->config = times;
    return n;
}

iot_bt_node_t *iot_bt_delay_create(iot_bt_node_t **children, uint16_t count, uint32_t delay_ticks)
{
    iot_bt_node_t *n = bt_alloc();
    if (n == NULL) {
        return NULL;
    }
    n->type = IOT_BT_DELAY;
    n->tick = bt_tick_delay;
    n->children = children;
    n->child_count = count;
    n->config = delay_ticks;
    return n;
}

iot_bt_node_t *iot_bt_action_create(iot_bt_status_t (*fn)(iot_bt_node_t *node), void *ctx)
{
    iot_bt_node_t *n = bt_alloc();
    if (n == NULL) {
        return NULL;
    }
    n->type = IOT_BT_ACTION;
    n->tick = bt_tick_action;
    n->action_fn = fn;
    n->ctx = ctx;
    return n;
}

iot_bt_node_t *iot_bt_condition_create(bool (*fn)(iot_bt_node_t *node), void *ctx)
{
    iot_bt_node_t *n = bt_alloc();
    if (n == NULL) {
        return NULL;
    }
    n->type = IOT_BT_CONDITION;
    n->tick = bt_tick_condition;
    n->cond_fn = fn;
    n->ctx = ctx;
    return n;
}

static iot_bt_status_t bt_tick_sequence(iot_bt_node_t *n)
{
    uint16_t i;
    for (i = 0; i < n->child_count; i++) {
        iot_bt_node_t *c = n->children[i];
        iot_bt_status_t s = c->tick(c);
        if (s == IOT_BT_FAILURE) {
            return IOT_BT_FAILURE;
        }
        if (s == IOT_BT_RUNNING) {
            return IOT_BT_RUNNING;
        }
    }
    return IOT_BT_SUCCESS;
}

static iot_bt_status_t bt_tick_selector(iot_bt_node_t *n)
{
    uint16_t i;
    for (i = 0; i < n->child_count; i++) {
        iot_bt_node_t *c = n->children[i];
        iot_bt_status_t s = c->tick(c);
        if (s == IOT_BT_SUCCESS) {
            return IOT_BT_SUCCESS;
        }
        if (s == IOT_BT_RUNNING) {
            return IOT_BT_RUNNING;
        }
    }
    return IOT_BT_FAILURE;
}

static iot_bt_status_t bt_tick_parallel(iot_bt_node_t *n)
{
    uint16_t i;
    bool any_fail = false;
    bool all_success = true;

    for (i = 0; i < n->child_count; i++) {
        iot_bt_status_t s = n->children[i]->tick(n->children[i]);
        if (s == IOT_BT_FAILURE) {
            any_fail = true;
        }
        if (s != IOT_BT_SUCCESS) {
            all_success = false;
        }
    }
    if (any_fail) {
        return IOT_BT_FAILURE;
    }
    if (all_success) {
        return IOT_BT_SUCCESS;
    }
    return IOT_BT_RUNNING;
}

static iot_bt_status_t bt_tick_inverter(iot_bt_node_t *n)
{
    iot_bt_node_t *c = (n->child_count > 0) ? n->children[0] : NULL;
    iot_bt_status_t s;

    if (c == NULL) {
        return IOT_BT_FAILURE;
    }
    s = c->tick(c);
    if (s == IOT_BT_SUCCESS) {
        return IOT_BT_FAILURE;
    }
    if (s == IOT_BT_FAILURE) {
        return IOT_BT_SUCCESS;
    }
    return IOT_BT_RUNNING;
}

static iot_bt_status_t bt_tick_repeat(iot_bt_node_t *n)
{
    iot_bt_node_t *c = (n->child_count > 0) ? n->children[0] : NULL;

    if (c == NULL) {
        return IOT_BT_FAILURE;
    }
    if (n->counter < 0) {
        n->counter = (int32_t)n->config;
    }
    while (n->counter > 0) {
        iot_bt_status_t s = c->tick(c);
        if (s == IOT_BT_FAILURE) {
            n->counter = -1;
            return IOT_BT_FAILURE;
        }
        if (s == IOT_BT_RUNNING) {
            return IOT_BT_RUNNING;
        }
        n->counter--;
        if (n->counter > 0 && c->reset != NULL) {
            c->reset(c);
        }
    }
    n->counter = -1;
    return IOT_BT_SUCCESS;
}

static iot_bt_status_t bt_tick_delay(iot_bt_node_t *n)
{
    iot_bt_node_t *c = (n->child_count > 0) ? n->children[0] : NULL;

    if (c == NULL) {
        return IOT_BT_FAILURE;
    }
    if (n->counter < 0) {
        n->counter = (int32_t)s_bt_tick;
    }
    if ((s_bt_tick - (uint32_t)n->counter) < n->config) {
        return IOT_BT_RUNNING;
    }
    return c->tick(c);
}

static iot_bt_status_t bt_tick_action(iot_bt_node_t *n)
{
    return (n->action_fn != NULL) ? n->action_fn(n) : IOT_BT_FAILURE;
}

static iot_bt_status_t bt_tick_condition(iot_bt_node_t *n)
{
    bool ok = (n->cond_fn != NULL) ? n->cond_fn(n) : false;
    return ok ? IOT_BT_SUCCESS : IOT_BT_FAILURE;
}