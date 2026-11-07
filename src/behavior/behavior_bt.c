/*
 * 行为树实现（运行期节点池 + 递归 tick）。
 *
 * 数据结构：
 *   - s_nodes[]：固定容量节点池（首次创建节点时惰性分配），创建后常驻，不做释放（容量 32）；
 *   - s_used[]：节点池占用标记，bt_alloc 线性查找第一个空闲槽；
 *   - s_bt_tick：全局帧计数，每次 behavior_bt_tick 自增，delay 节点据此计时。
 *
 * 算法流程：
 *   1) behavior_bt_tick 先递增帧号，再调用根节点 tick；
 *   2) 各组合节点按自身语义遍历子节点并归并三态结果；
 *   3) repeat 用 counter 记录“剩余次数”，delay 用 counter 记录“起始帧号”，
 *      二者均以 counter = -1 表示“尚未初始化/已结束”。
 *
 * 边界与注意事项：
 *   - 节点池满时创建函数返回 NULL，调用方必须判空；
 *   - 组合节点未对 children[i] 为 NULL 做保护（halt/reset 递归除外），
 *     因此传入的 children 数组元素必须有效；
 *   - 未实现“运行中的子节点记忆”，sequence/selector 每帧都从第 0 个重新评估。
 */
#include "behavior_bt.h"
#include "port_mem.h"
#include <string.h>

/* 节点池容量：创建节点总数上限，超出后 bt_alloc 返回 NULL */
#define BEHAVIOR_BT_MAX_NODES 32

static behavior_bt_node_t *s_nodes = NULL;   /* 节点池（惰性分配，容量 BEHAVIOR_BT_MAX_NODES） */
static bool *s_used = NULL;                  /* 节点池占用标记（惰性分配） */
static uint32_t s_bt_tick = 0;               /* 全局帧计数（delay 计时基准） */

/*
 * 惰性分配节点池与占用标记；已分配则直接复用，不重复分配。
 * @return 0 成功（已分配或本次分配成功）；-1 分配失败
 */
static int bt_ensure(void)
{
    if (s_nodes != NULL && s_used != NULL) {
        return 0;
    }
    if (s_nodes == NULL) {
        s_nodes = (behavior_bt_node_t *)port_calloc(BEHAVIOR_BT_MAX_NODES, sizeof(*s_nodes));
    }
    if (s_used == NULL) {
        s_used = (bool *)port_calloc(BEHAVIOR_BT_MAX_NODES, sizeof(*s_used));
    }
    if (s_nodes == NULL || s_used == NULL) {
        port_free(s_nodes);
        port_free(s_used);
        s_nodes = NULL;
        s_used = NULL;
        return -1;                  /* 分配失败：清空便于后续重试 */
    }
    return 0;
}

/*
 * 从节点池分配一个节点。
 * @return 节点指针（内容已清零、counter 置 -1）；池满或分配失败返回 NULL
 */
static behavior_bt_node_t *bt_alloc(void)
{
    int i;
    if (bt_ensure() != 0) {
        return NULL;                /* 节点池不可用 */
    }
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

/*
 * 递归停止子树：先深度遍历子节点，再调用本节点 halt 回调（后序）。
 * 采用后序是为了保证“子先停、父后停”，避免父节点先释放子节点依赖的资源。
 * @param n 子树根节点，可为 NULL
 */
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

/*
 * 递归复位子树：先清除本节点 counter（-1 = 未初始化），再后序复位子节点。
 * counter 复位使 repeat 重新计数、delay 重新计时。
 * @param n 子树根节点，可为 NULL
 */
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

/*
 * 初始化行为树：清空全局帧计数，并递归复位整棵树。
 * @param root 根节点，可为 NULL（此时仅清帧计数并返回 -1）
 * @return 0 成功；root 为 NULL 或节点池分配失败返回 -1
 */
int behavior_bt_init(behavior_bt_node_t *root)
{
    s_bt_tick = 0;
    if (root == NULL) {
        return -1;
    }
    if (bt_ensure() != 0) {
        return -1;                  /* 节点池分配失败 */
    }
    bt_reset_rec(root);
    return 0;
}

/*
 * 推进一帧并返回根节点结果。
 * @param root 根节点
 * @return 三态结果；root 为 NULL 时返回 FAILURE（帧计数不递增）
 * @note 帧号在调用根 tick 之前递增，因此 delay 节点首次 tick 记录的是当帧帧号。
 */
behavior_bt_status_t behavior_bt_tick(behavior_bt_node_t *root)
{
    if (root == NULL) {
        return BEHAVIOR_BT_FAILURE;
    }
    s_bt_tick++;
    return root->tick(root);
}

/*
 * 停止整棵树：递归调用各节点 halt 回调。
 * @param root 根节点
 * @return 0 成功；root 为 NULL 返回 -1
 */
int behavior_bt_halt(behavior_bt_node_t *root)
{
    if (root == NULL) {
        return -1;
    }
    bt_halt_rec(root);
    return 0;
}

/*
 * 复位整棵树：递归清除 counter 与各节点私有状态。
 * @param root 根节点
 * @return 0 成功；root 为 NULL 返回 -1
 */
int behavior_bt_reset(behavior_bt_node_t *root)
{
    if (root == NULL) {
        return -1;
    }
    bt_reset_rec(root);
    return 0;
}

/* ---------------- 节点创建：统一从节点池分配并绑定 tick 实现 ---------------- */

/* 顺序节点：从左到右执行，遇 FAILURE 立即返回，遇 RUNNING 挂起，全部 SUCCESS 才 SUCCESS */
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

/* 选择节点：从左到右尝试，遇 SUCCESS 立即返回，遇 RUNNING 挂起，全部 FAILURE 才 FAILURE */
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

/* 并行节点：同一帧内 tick 所有子节点，任一 FAILURE 即 FAILURE，全部 SUCCESS 才 SUCCESS */
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

/* 取反节点：仅使用 children[0]，SUCCESS↔FAILURE 互换，RUNNING 原样返回 */
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

/* 重复节点：config 记录总次数，counter 在首次 tick 时初始化为总次数 */
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

/* 延时节点：config 记录需要等待的帧数，counter 在首次 tick 时记录起始帧号 */
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

/* 动作叶子：fn 为空时 tick 返回 FAILURE */
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

/* 条件叶子：fn 为空时按 false 处理（返回 FAILURE） */
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

/* 顺序节点 tick：短路求值，第一个 FAILURE 即失败；RUNNING 逐帧返回直到子节点完成 */
static behavior_bt_status_t bt_tick_sequence(behavior_bt_node_t *n)
{
    uint16_t i;
    for (i = 0; i < n->child_count; i++) {
        behavior_bt_node_t *c = n->children[i];
        behavior_bt_status_t s = c->tick(c);
        if (s == BEHAVIOR_BT_FAILURE) {
            return BEHAVIOR_BT_FAILURE;     /* 任一子节点失败：整条顺序失败 */
        }
        if (s == BEHAVIOR_BT_RUNNING) {
            return BEHAVIOR_BT_RUNNING;     /* 当前子节点未完成：本帧到此为止 */
        }
    }
    return BEHAVIOR_BT_SUCCESS;             /* 所有子节点均成功 */
}

/* 选择节点 tick：短路求值，第一个 SUCCESS 即成功；RUNNING 逐帧返回直到子节点完成 */
static behavior_bt_status_t bt_tick_selector(behavior_bt_node_t *n)
{
    uint16_t i;
    for (i = 0; i < n->child_count; i++) {
        behavior_bt_node_t *c = n->children[i];
        behavior_bt_status_t s = c->tick(c);
        if (s == BEHAVIOR_BT_SUCCESS) {
            return BEHAVIOR_BT_SUCCESS;     /* 任一子节点成功：整个选择成功 */
        }
        if (s == BEHAVIOR_BT_RUNNING) {
            return BEHAVIOR_BT_RUNNING;     /* 当前子节点未完成：本帧到此为止 */
        }
    }
    return BEHAVIOR_BT_FAILURE;             /* 所有子节点均失败 */
}

/* 并行节点 tick：不短路，本帧把所有子节点都推进一次；失败优先级高于运行中 */
static behavior_bt_status_t bt_tick_parallel(behavior_bt_node_t *n)
{
    uint16_t i;
    bool any_fail = false;
    bool all_success = true;

    for (i = 0; i < n->child_count; i++) {
        behavior_bt_status_t s = n->children[i]->tick(n->children[i]);
        if (s == BEHAVIOR_BT_FAILURE) {
            any_fail = true;        /* 只要有一个失败，最终就是失败 */
        }
        if (s != BEHAVIOR_BT_SUCCESS) {
            all_success = false;    /* 出现 RUNNING/FAILURE 即非全部成功 */
        }
    }
    if (any_fail) {
        return BEHAVIOR_BT_FAILURE;
    }
    if (all_success) {
        return BEHAVIOR_BT_SUCCESS;
    }
    return BEHAVIOR_BT_RUNNING;     /* 无失败但仍有未完成子节点 */
}

/* 取反节点 tick：只取第一个子节点，成功/失败互换，RUNNING 透传 */
static behavior_bt_status_t bt_tick_inverter(behavior_bt_node_t *n)
{
    behavior_bt_node_t *c = (n->child_count > 0) ? n->children[0] : NULL;
    behavior_bt_status_t s;

    if (c == NULL) {
        return BEHAVIOR_BT_FAILURE;     /* 无子节点：无法取反，视为失败 */
    }
    s = c->tick(c);
    if (s == BEHAVIOR_BT_SUCCESS) {
        return BEHAVIOR_BT_FAILURE;
    }
    if (s == BEHAVIOR_BT_FAILURE) {
        return BEHAVIOR_BT_SUCCESS;
    }
    return BEHAVIOR_BT_RUNNING;         /* RUNNING 不做取反 */
}

/*
 * 重复节点 tick：把 children[0] 执行 config 次。
 * counter 语义：-1 表示未初始化（本帧取 config），>0 表示剩余次数。
 * 同一帧内会用 while 循环连续执行已完成的次数，直到子节点返回 RUNNING 或次数耗尽；
 * 每完成一次后复位子节点，使下一次从干净状态开始。
 */
static behavior_bt_status_t bt_tick_repeat(behavior_bt_node_t *n)
{
    behavior_bt_node_t *c = (n->child_count > 0) ? n->children[0] : NULL;

    if (c == NULL) {
        return BEHAVIOR_BT_FAILURE;     /* 无子节点可重复 */
    }
    if (n->counter < 0) {
        n->counter = (int32_t)n->config;    /* 首次 tick：装载总次数 */
    }
    while (n->counter > 0) {
        behavior_bt_status_t s = c->tick(c);
        if (s == BEHAVIOR_BT_FAILURE) {
            n->counter = -1;                /* 失败即放弃剩余次数，回到未初始化态 */
            return BEHAVIOR_BT_FAILURE;
        }
        if (s == BEHAVIOR_BT_RUNNING) {
            return BEHAVIOR_BT_RUNNING;     /* 本轮未完成：保留剩余次数，下一帧继续 */
        }
        n->counter--;                       /* 本轮成功，消耗一次 */
        if (n->counter > 0 && c->reset != NULL) {
            c->reset(c);                    /* 还有剩余次数：复位子节点再执行下一轮 */
        }
    }
    n->counter = -1;                        /* 次数用尽：恢复未初始化态 */
    return BEHAVIOR_BT_SUCCESS;
}

/*
 * 延时节点 tick：等待 config 帧后再转发子节点结果。
 * counter 语义：-1 表示未开始（本帧记录起始帧号），否则为起始帧号。
 * 判定条件为“当前帧 - 起始帧 >= config”，因此首次 tick 当帧只记录不执行。
 */
static behavior_bt_status_t bt_tick_delay(behavior_bt_node_t *n)
{
    behavior_bt_node_t *c = (n->child_count > 0) ? n->children[0] : NULL;

    if (c == NULL) {
        return BEHAVIOR_BT_FAILURE;     /* 无子节点可延时执行 */
    }
    if (n->counter < 0) {
        n->counter = (int32_t)s_bt_tick;    /* 首次 tick：记录起始帧号 */
    }
    if ((s_bt_tick - (uint32_t)n->counter) < n->config) {
        return BEHAVIOR_BT_RUNNING;     /* 延时未满，本帧不执行子节点 */
    }
    return c->tick(c);                  /* 延时结束：结果与子节点一致 */
}

/* 动作叶子 tick：直接转发使用方回调结果；回调为空视为失败 */
static behavior_bt_status_t bt_tick_action(behavior_bt_node_t *n)
{
    return (n->action_fn != NULL) ? n->action_fn(n) : BEHAVIOR_BT_FAILURE;
}

/* 条件叶子 tick：条件为真映射 SUCCESS，否则 FAILURE；回调为空按 false 处理 */
static behavior_bt_status_t bt_tick_condition(behavior_bt_node_t *n)
{
    bool ok = (n->cond_fn != NULL) ? n->cond_fn(n) : false;
    return ok ? BEHAVIOR_BT_SUCCESS : BEHAVIOR_BT_FAILURE;
}