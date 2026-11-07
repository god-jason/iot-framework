/*
 * fsm.h - 有限状态机（表驱动）接口
 *
 * 状态与转换均由调用者以静态数组提供，模块只保存指针、不分配内存。
 * 事件通过 fsm_dispatch() 同步派发，无内置事件队列。
 */

#ifndef FSM_H
#define FSM_H

/*
 * 有限状态机（无内置事件队列）。
 * 事件由调用者直接传入 fsm_dispatch()，状态机只负责状态与转换逻辑。
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct fsm fsm_t;
typedef struct fsm_state fsm_state_t;

/* 进入状态回调：切换完成后调用 */
typedef void (*fsm_entry_fn)(fsm_t *fsm, void *ctx);
/* 事件处理回调：每次 dispatch 先在当前状态执行，早于转换判断 */
typedef void (*fsm_action_fn)(fsm_t *fsm, void *ctx,
                                  uint32_t event, void *data);
/* 离开状态回调：转换/停止前调用 */
typedef void (*fsm_exit_fn)(fsm_t *fsm, void *ctx);
/* 转换守卫：返回 true 允许转换，false 拒绝本次转换 */
typedef bool (*fsm_guard_fn)(fsm_t *fsm, void *ctx,
                                 uint32_t event, void *data);

/* 状态定义（通常以静态数组形式给出） */
struct fsm_state {
    uint32_t id;   /* 状态 ID，需在状态表内唯一 */

    fsm_entry_fn entry;    /* 进入回调，可为 NULL */
    fsm_action_fn action;  /* 事件处理回调，可为 NULL */
    fsm_exit_fn exit;      /* 离开回调，可为 NULL */

    void *ctx;     /* 状态私有上下文，回调时作为 ctx 传入 */
};

/* 转换规则：from 状态下收到 event 且 guard 通过时切换到 to */
typedef struct {
    uint32_t from;   /* 源状态 ID */
    uint32_t event;  /* 触发事件 ID */
    uint32_t to;     /* 目标状态 ID（必须存在于状态表） */

    fsm_guard_fn guard;   /* 守卫回调，NULL 表示无条件通过 */
} fsm_transition_t;

/* 状态机实例（由调用者分配，通常为静态变量） */
struct fsm {
    const fsm_state_t *states;   /* 状态表，需长期有效 */
    uint16_t state_count;                  /* 状态个数 */

    const fsm_transition_t *transitions;  /* 转换表，可为 NULL */
    uint16_t transition_count;                      /* 转换条目数 */

    const fsm_state_t *current;   /* 当前状态，NULL 表示未启动 */

    void *ctx;                              /* 用户上下文（初始化时传入） */

    bool running;                           /* 是否已启动 */
    bool paused;                            /* 是否暂停（暂停后拒绝 dispatch） */
};

/*
 * 初始化状态机（不进入任何状态）。
 * 参数：fsm - 状态机对象；states/state_count - 状态表；transitions/transition_count
 *       - 转换表（可为 NULL/0）；ctx - 用户上下文
 * 返回：0 成功；-1 参数非法（fsm/states 为 NULL 或 state_count 为 0）
 */
int fsm_init(fsm_t *fsm,
                 const fsm_state_t *states, uint16_t state_count,
                 const fsm_transition_t *transitions, uint16_t transition_count,
                 void *ctx);

/* 启动并进入初始状态（执行其 entry）；返回 0，-1 表示状态不存在 */
int fsm_start(fsm_t *fsm, uint32_t initial_state);
/* 停止状态机（运行中会执行当前状态 exit）；返回 0，-1 表示 fsm 为 NULL */
int fsm_stop(fsm_t *fsm);

/* 暂停（不触发 exit，dispatch 将被拒绝）；返回 0，-1 表示 fsm 为 NULL */
int fsm_pause(fsm_t *fsm);
/* 恢复（仅清除暂停标志）；返回 0，-1 表示 fsm 为 NULL */
int fsm_resume(fsm_t *fsm);

/*
 * 派发事件：先执行当前状态 action，再按转换表切换状态。
 * 参数：fsm - 状态机；event - 事件 ID；data - 事件数据
 * 返回：0 处理完成（含无匹配转换/被 guard 拒绝）；
 *       -1 表示未启动、已暂停、参数非法或目标状态不存在
 */
int fsm_dispatch(fsm_t *fsm, uint32_t event, void *data);
/* 强制切换状态（绕过转换表，执行 exit/entry）；返回 0，-1 表示失败 */
int fsm_transition(fsm_t *fsm, uint32_t state_id);

/* 当前状态 ID；未启动时返回 0 */
uint32_t fsm_get_state(const fsm_t *fsm);
/* 当前是否处于指定状态 */
bool fsm_is_state(const fsm_t *fsm, uint32_t state_id);
/* 是否已启动（暂停时仍为 true） */
bool fsm_is_running(const fsm_t *fsm);

#ifdef __cplusplus
}
#endif

#endif /* FSM_H */
