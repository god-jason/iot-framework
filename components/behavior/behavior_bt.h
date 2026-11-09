#ifndef BEHAVIOR_BT_H
#define BEHAVIOR_BT_H

/*
 * 行为树（Behavior Tree）。
 * 组合节点（sequence/selector/parallel/inverter/repeat/delay）每次 tick 重新从头评估子节点；
 * repeat / delay 通过节点内部 state 保持进度。
 * 节点由内部静态节点池分配，无动态内存。
 *
 * 模块职责：以“每帧 tick”的方式描述机器人任务逻辑，把条件判断（condition）、
 * 具体动作（action）与组合语义（顺序/选择/并行/取反/重复/延时）解耦。
 *
 * 执行约定：
 *   - tick 返回 SUCCESS / FAILURE / RUNNING 三态；
 *   - 组合节点每次 tick 从第 0 个子节点重新评估（无记忆），因此子节点必须可重复调用；
 *   - 需要跨 tick 记忆的语义由 repeat/delay 的 counter 以及使用方 action 自身实现；
 *   - 节点池容量 BEHAVIOR_BT_MAX_NODES（见 .c），节点一经创建不会回收，属于静态分配。
 *
 * 注意事项：
 *   - children 数组生命周期须由使用方保证（框架只保存指针，不拷贝）；
 *   - 未实现“运行中节点缓存”，若子节点有副作用需自行保证幂等；
 *   - 非线程安全，tick 与节点创建应在同一任务上下文中串行调用。
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 节点 tick 返回值（三态），统一以 int 返回，枚举仅用于定义常量名 */
enum {
    BEHAVIOR_BT_SUCCESS,    /* 成功：该子树本帧已完成 */
    BEHAVIOR_BT_FAILURE,    /* 失败：该子树本帧无法完成 */
    BEHAVIOR_BT_RUNNING     /* 运行中：还需后续 tick 继续推进 */
};

/* 节点类型：前 7 种为组合/叶子语义，最后一种为延时装饰器 */
typedef enum {
    BEHAVIOR_BT_SEQUENCE,   /* 顺序：依次执行，任一失败即失败，全成功才成功 */
    BEHAVIOR_BT_SELECTOR,   /* 选择：依次尝试，任一成功即成功，全失败才失败 */
    BEHAVIOR_BT_PARALLEL,   /* 并行：一帧内 tick 所有子节点，任一失败即失败 */
    BEHAVIOR_BT_INVERTER,   /* 取反：对唯一子节点结果取反（RUNNING 保持） */
    BEHAVIOR_BT_REPEAT,     /* 重复：将子节点执行 config 次 */
    BEHAVIOR_BT_ACTION,     /* 叶子：调用使用方动作回调 */
    BEHAVIOR_BT_CONDITION,  /* 叶子：调用使用方条件回调（true→SUCCESS） */
    BEHAVIOR_BT_DELAY       /* 延时：等待 config 个 tick 后再 tick 子节点 */
} behavior_bt_type_e;

typedef struct behavior_bt_node behavior_bt_node_t;

/* 行为树节点：组合节点与叶子节点共用同一结构，按 type 选择 tick 实现 */
struct behavior_bt_node {
    behavior_bt_type_e type;    /* 节点类型，决定 tick 的默认实现 */

    /*
     * 本节点的 tick 实现。创建函数会自动绑定为对应类型的内部实现；
     * 使用方也可在创建后自行替换以实现自定义节点。
     * 参数为节点自身，返回三态结果。
     */
    int (*tick)(behavior_bt_node_t *node);

    /* 可选：停止回调，递归 halt 时在子节点之后调用，用于停止正在进行的动作 */
    void (*halt)(behavior_bt_node_t *node);

    /* 可选：复位回调，递归 reset 时在子节点之后调用，用于清理节点私有状态 */
    void (*reset)(behavior_bt_node_t *node);

    behavior_bt_node_t **children;  /* 子节点指针数组（不拷贝，外部持有） */
    uint16_t child_count;           /* 子节点个数 */

    void *ctx;                      /* 使用方上下文，透传给 action/condition 回调 */

    /* ---- 以下为框架内部字段 ---- */
    int (*action_fn)(behavior_bt_node_t *node); /* action 行为 */
    bool (*cond_fn)(behavior_bt_node_t *node);              /* condition 判断 */

    int32_t  counter;  /* repeat 剩余次数 / delay 起始 tick */
    uint32_t config;   /* repeat 次数 / delay ticks */
};

/*
 * 初始化行为树：清零全局 tick 计数并递归复位整棵树。
 * @param root 根节点，可为 NULL（仅清零 tick 计数）
 * @return 0；root 为 NULL 时返回 -1
 */
int behavior_bt_init(behavior_bt_node_t *root);

/*
 * 推进一帧：先递增全局 tick 计数（delay 节点依赖它计时），再调用根节点 tick。
 * @param root 根节点
 * @return 根节点返回的三态结果；root 为 NULL 时返回 FAILURE
 */
int behavior_bt_tick(behavior_bt_node_t *root);

/*
 * 递归停止：先停止子树，再调用各节点的 halt 回调。
 * @param root 根节点
 * @return 0；root 为 NULL 时返回 -1
 */
int behavior_bt_halt(behavior_bt_node_t *root);

/*
 * 递归复位：将各节点 counter 置 -1（清除 repeat/delay 进度）并调用 reset 回调。
 * @param root 根节点
 * @return 0；root 为 NULL 时返回 -1
 * @note 复位后 repeat/delay 会重新开始计时/计数。
 */
int behavior_bt_reset(behavior_bt_node_t *root);

/* 创建一个组合节点并绑定对应 tick 实现；节点池耗尽时返回 NULL。
 * @param children 子节点数组（外部持有，不拷贝），可为 NULL（等价于无子节点）
 * @param count    子节点个数
 * @return 新节点指针；失败返回 NULL
 */
behavior_bt_node_t *behavior_bt_sequence_create(behavior_bt_node_t **children, uint16_t count);   /* 顺序节点 */
behavior_bt_node_t *behavior_bt_selector_create(behavior_bt_node_t **children, uint16_t count);   /* 选择节点 */
behavior_bt_node_t *behavior_bt_parallel_create(behavior_bt_node_t **children, uint16_t count);   /* 并行节点 */
behavior_bt_node_t *behavior_bt_inverter_create(behavior_bt_node_t **children, uint16_t count);   /* 取反节点（只用 children[0]） */

/*
 * 重复节点：把 children[0] 连续执行 times 次。
 * @param times 重复次数；0 表示首次 tick 即成功
 * @return 新节点指针；失败返回 NULL
 */
behavior_bt_node_t *behavior_bt_repeat_create(behavior_bt_node_t **children, uint16_t count, uint16_t times);

/*
 * 延时节点：创建后等待 delay_ticks 帧再 tick children[0]。
 * @param delay_ticks 延时帧数，按 behavior_bt_tick 的调用次数计
 * @return 新节点指针；失败返回 NULL
 */
behavior_bt_node_t *behavior_bt_delay_create(behavior_bt_node_t **children, uint16_t count, uint32_t delay_ticks);

/*
 * 创建动作叶子节点。
 * @param fn  动作回调，返回三态；为 NULL 时 tick 返回 FAILURE
 * @param ctx 透传给 fn 的上下文（fn 内通过 node->ctx 取用）
 * @return 新节点指针；失败返回 NULL
 */
behavior_bt_node_t *behavior_bt_action_create(int (*fn)(behavior_bt_node_t *node), void *ctx);

/*
 * 创建条件叶子节点。
 * @param fn  条件回调，返回 true 时节点 tick 为 SUCCESS，false 为 FAILURE
 * @param ctx 透传给 fn 的上下文
 * @return 新节点指针；失败返回 NULL
 */
behavior_bt_node_t *behavior_bt_condition_create(bool (*fn)(behavior_bt_node_t *node), void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* BEHAVIOR_BT_H */