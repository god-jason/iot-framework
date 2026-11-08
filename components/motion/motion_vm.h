#ifndef MOTION_VM_H
#define MOTION_VM_H

/*
 * 虚拟机（VM）公共定义。
 *
 * 职责：保存一段脚本的执行上下文（PC、行号、流程栈、变量、等待状态、
 * 错误信息），并负责取指、解析、调用执行器、轮询等待与异常跳转。
 * 主程序使用 motion.c 中的静态 VM；每个 JOB 在 motion_task.c 中各自
 * 持有一个 motion_vm_t，因此同一套 VM 逻辑可被多段程序复用。
 *
 * 执行模型：事件驱动、非阻塞。motion_vm_poll() 每次最多执行
 * MOTION_MAX_EXEC_PER_POLL 条指令，遇到 WAIT 立即返回，等待条件满足
 * 后从同一位置继续；JOB 之间通过 motion_task_poll() 轮转调度。
 *
 * 依赖：motion_source（取行）、motion_parser（解析）、motion_flow
 *       （流程索引/表达式）、motion_executor（执行）、motion_variable、
 *       motion_error、port（tick 计时）。
 */

#include "motion_source.h"
#include "motion_types.h"
#include "motion_variable.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 循环上下文：一个 LOOP 块在运行期的状态 */
typedef struct {
    uint32_t body_offset;   /* 循环体首行偏移，用于回跳 */
    uint32_t endloop_next;  /* ENDLOOP 之后偏移，用于退出循环 */
    uint32_t loop_offset;   /* LOOP 指令自身偏移 */
    int64_t total;          /* 总循环次数（无限循环时为 0） */
    int64_t current;        /* 已完成的迭代次数 */
    bool infinite;          /* 是否为无次数参数的无限循环 */
} motion_loop_context_t;

/* IF 上下文：记录已进入的 IF 块，用于跳转时清理嵌套关系 */
typedef struct {
    uint32_t if_offset;     /* IF 指令偏移 */
    uint32_t end_next;      /* 配对 END 之后的偏移（块的范围上界） */
} motion_if_context_t;

/*
 * VM 运行上下文。
 * 注意：结构体较大（含变量表与多层栈），应静态分配，避免放在栈上。
 */
typedef struct motion_vm {
    motion_state_t state;        /* 当前运行状态 */
    motion_state_t resume_state; /* 暂停前的状态，供 resume 恢复 */

    uint32_t pc;                 /* 程序计数器：下一条待取指令的字节偏移 */
    uint32_t line;               /* 当前源码行号（用于报错） */
    uint32_t start_pc;           /* 启动入口偏移（主程序为 0，JOB 为 JOB 体首行） */
    uint32_t end_offset;         /* 段结束偏移；bounded 为真时到达即结束 */

    bool bounded;                /* 是否限制在本段内执行（JOB 使用） */
    int segment;                 /* 段号：0 主程序，>0 JOB 段 */
    bool handling_error;         /* 是否正在执行 ON ERROR 处理块 */

    motion_source_t *source;     /* 脚本数据源（不拥有其生命周期） */

    uint32_t call_stack[MOTION_MAX_CALL_DEPTH]; /* CALL 返回地址栈 */
    uint8_t call_depth;                          /* 当前调用深度 */

    motion_loop_context_t loop_stack[MOTION_MAX_LOOP_DEPTH]; /* 循环栈 */
    uint8_t loop_depth;                                      /* 当前循环深度 */

    motion_if_context_t if_stack[MOTION_MAX_IF_DEPTH]; /* IF 栈 */
    uint8_t if_depth;                                  /* 当前 IF 深度 */

    motion_variable_table_t variables; /* 本段变量表 */
    motion_wait_t wait;                /* 当前等待条件 */
    motion_error_t error;              /* 最近一次错误 */
} motion_vm_t;

/* 初始化 VM：清零全部字段，状态置 IDLE，变量与错误表重置 */
void motion_vm_init(motion_vm_t *vm);

/*
 * 加载数据源并绑定到脚本起点。
 * 参数：vm VM 上下文；source 数据源。
 * 返回：0 成功；MOTION_ERR_PARAMETER 参数为空。
 */
int motion_vm_load(motion_vm_t *vm, motion_source_t *source);

/*
 * 绑定数据源与执行范围。
 * 参数：
 *   source      - 数据源；
 *   start_pc    - 入口偏移；
 *   segment     - 段号；
 *   bounded     - 是否限制在本段范围内执行；
 *   end_offset  - 段结束偏移。
 * 注意：只设置上下文，不改变已产生的运行状态。
 */
void motion_vm_bind(motion_vm_t *vm, motion_source_t *source,
                    uint32_t start_pc, int segment, bool bounded,
                    uint32_t end_offset);

/*
 * 启动执行：重置运行栈、定位 pc、打开数据源并 seek 到入口。
 * 返回：0 成功；MOTION_ERR_PARAMETER 参数为空；MOTION_ERR_FLOW seek 失败。
 */
int motion_vm_start(motion_vm_t *vm);

/* 暂停：仅对 RUNNING/WAITING 生效，记录 resume_state 后转为 PAUSED */
int motion_vm_pause(motion_vm_t *vm);

/* 恢复：仅对 PAUSED 生效，按 resume_state 回到 RUNNING 或 WAITING */
int motion_vm_resume(motion_vm_t *vm);

/* 正常停止：取消等待并置为 COMPLETED */
int motion_vm_stop(motion_vm_t *vm);

/* 中止：取消等待并置为 ABORTED */
int motion_vm_abort(motion_vm_t *vm);

/*
 * 调度一次 VM：处理等待完成、取指解析并执行，直到达到
 * MOTION_MAX_EXEC_PER_POLL 或进入非运行状态。
 */
void motion_vm_poll(motion_vm_t *vm);

/* 返回当前状态；vm 为 NULL 时返回 MOTION_STATE_IDLE */
motion_state_t motion_vm_get_state(const motion_vm_t *vm);

/*
 * 跳转到指定偏移：更新 pc 与行号，并按目标位置清理失去作用域的
 * IF/LOOP 上下文。
 */
void motion_vm_jump(motion_vm_t *vm, uint32_t offset);

/*
 * 按目标偏移清理流程栈：弹出所有不再包含 target 的 IF/LOOP 上下文。
 * 参数：target 新的 pc 值。
 */
void motion_vm_cleanup_flow(motion_vm_t *vm, uint32_t target);

/*
 * 记录异常。
 * 参数：code 错误码；message 附加消息（可为 NULL）。
 * 返回：原样返回 code，便于链式使用。
 */
int motion_vm_raise(motion_vm_t *vm, int code, const char *message);

/*
 * 检查当前等待是否结束。
 * 返回：1 已完成（VM 可继续运行）；0 仍在等待；-1 出错（错误已写入
 *       vm->error，包括超时）。
 * 注意：等待未激活时直接返回 1 并把状态置回 RUNNING。
 */
int motion_vm_check_wait(motion_vm_t *vm);

#ifdef __cplusplus
}
#endif

#endif /* MOTION_VM_H */
