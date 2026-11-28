#ifndef ACTION_H
#define ACTION_H

/*
 * 动作（Action）：具有执行过程的任务（如投料、移动、充电）。
 *
 * 模块职责：为“需要跨越多个控制周期才能完成”的行为提供统一的生命周期抽象。
 * 具体硬件/业务动作通过填充 start / update / cancel / reset 回调实现，
 * 框架本身不关心动作内部细节，只负责状态迁移与回调调度（典型的命令模式）。
 *
 * 状态机：
 *   IDLE --start()成功--> RUNNING --update()==0--> RUNNING（自循环）
 *                              |--update()>0--> SUCCESS
 *                              |--update()<0--> FAILED
 *   IDLE --start()失败--> FAILED
 *   任意态 --cancel()--> CANCELLED
 *   任意态 --reset()--> IDLE
 *
 * update() 回调约定返回值：
 *   0  -> 继续运行（RUNNING）
 *   >0 -> 完成（SUCCESS）
 *   <0 -> 失败（FAILED）
 *
 * 注意事项：
 *   - 回调可为 NULL，框架按“所有回调都成功”处理（update 为 NULL 视为立即完成）。
 *   - 各接口传入 NULL 指针统一返回 -1，不做任何状态修改（查询接口返回 IDLE）。
 *   - action_t 的 ctx 由使用方自行管理生命周期，框架不分配、不释放。
 *   - 本模块无动态内存、无锁，非线程安全，需由单一线程/任务调用。
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 动作生命周期状态 */
typedef enum {
    ACTION_IDLE,        /* 空闲：初始化后或 reset 后的初态，尚未开始执行 */
    ACTION_RUNNING,     /* 运行中：start 成功且 update 尚未给出终态 */
    ACTION_SUCCESS,     /* 成功：update 返回 >0 后进入的终态 */
    ACTION_FAILED,      /* 失败：start 或 update 返回负值后进入的终态 */
    ACTION_CANCELLED    /* 已取消：调用 cancel 后的终态 */
} action_state_e;

typedef struct action action_t;

/*
 * 动作描述（由使用方静态定义并填充回调）
 * 所有回调的第一个参数均为动作自身，便于通过 action->ctx 取回私有数据。
 */
struct action {
    const char *name;   /* 动作名称（调试/日志用，可为 NULL） */

    /*
     * 启动回调：在 IDLE 状态下被 action_start() 调用。
     * 返回 0(0) 表示启动成功并进入 RUNNING；返回非 0 则直接进入 FAILED。
     */
    int (*start)(action_t *action);

    /*
     * 周期回调：在 RUNNING 状态下每个调度周期被 action_update() 调用。
     * 返回 0 表示继续运行，>0 表示完成（SUCCESS），<0 表示失败（FAILED）。
     */
    int (*update)(action_t *action);

    /*
     * 取消回调：被 action_cancel() 调用，用于释放资源或停止执行机构。
     * 无论返回何值，状态都会置为 CANCELLED。
     */
    int (*cancel)(action_t *action);

    /*
     * 复位回调：被 action_reset() 调用，用于清理内部进度以便重新执行。
     * 无论返回何值，状态都会置为 IDLE。
     */
    int (*reset)(action_t *action);

    action_state_e state;   /* 当前状态，由框架维护，外部只读 */

    void *ctx;              /* 使用方私有数据指针，框架原样保存 */
};

/*
 * 初始化动作：将 state 置为 ACTION_IDLE。
 * 只重置状态，不会清零 name/ctx/回调，这些字段需由使用方事先填好。
 * @param action 动作对象，不可为 NULL
 * @return 0(0) 成功；-1 参数为 NULL
 */
int action_init(action_t *action);

/*
 * 启动动作：调用 start 回调并将状态迁移到 RUNNING/FAILED。
 * @param action 动作对象，不可为 NULL
 * @return start 回调的返回值；参数为 NULL 时返回 -1
 * @note 回调为 NULL 时视为启动成功（0）。
 */
int action_start(action_t *action);

/*
 * 推进一个调度周期：仅在 RUNNING 状态下调用 update 回调。
 * @param action 动作对象，不可为 NULL
 * @return 0(0) 表示本次调用已处理（状态可能仍为 RUNNING）；
 *         -1 表示参数为 NULL 或当前状态不是 RUNNING（本次未执行回调）
 * @note 返回值反映“是否成功推进”，动作本身的成败需用 action_get_state() 查询。
 */
int action_update(action_t *action);

/*
 * 取消动作：调用 cancel 回调，无条件将状态置为 ACTION_CANCELLED。
 * @param action 动作对象，不可为 NULL
 * @return cancel 回调的返回值；参数为 NULL 时返回 -1
 */
int action_cancel(action_t *action);

/*
 * 复位动作：调用 reset 回调，无条件将状态置为 ACTION_IDLE，便于重新执行。
 * @param action 动作对象，不可为 NULL
 * @return reset 回调的返回值；参数为 NULL 时返回 -1
 */
int action_reset(action_t *action);

/*
 * 查询当前状态。
 * @param action 动作对象，可为 NULL
 * @return 当前状态；action 为 NULL 时返回 ACTION_IDLE
 */
action_state_e action_get_state(const action_t *action);

#ifdef __cplusplus
}
#endif

#endif /* ACTION_H */