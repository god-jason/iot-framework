#ifndef MOTION_EXECUTOR_H
#define MOTION_EXECUTOR_H

/*
 * 指令执行器接口。
 *
 * 职责：把一条已解析的 motion_instruction_t 翻译为对设备层
 * （motion_device）或任务层（motion_task）的具体调用，并处理参数解析、
 * 默认值与取值范围裁剪。执行器不负责取指与流程跳转，这些由 motion_vm
 * 完成；控制流指令（IF/LOOP/GOTO 等）也由本模块解释并调用 motion_vm_jump。
 *
 * 执行模型：纯同步调用。设备类指令立即返回 MOTION_EXEC_OK；
 * WAIT 类指令只填写 vm->wait 并返回 MOTION_EXEC_WAIT，由 VM 在后续
 * poll 中通过 motion_vm_check_wait() 轮询完成状态。
 *
 * 依赖：motion_vm（运行上下文）、motion_flow（流程索引/表达式求值）、
 *       motion_device（设备操作）、motion_task（JOB 控制）。
 */

#include "motion_types.h"
#include "motion_vm.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 执行单条指令。
 * 参数：
 *   vm          - 当前 VM 上下文，执行器会读写其变量表、等待状态、错误信息；
 *   instruction - 待执行的指令（line/offset/next_offset 已由 VM 填好）。
 * 返回：
 *   MOTION_EXEC_OK       - 本条指令完成，VM 继续下一条；
 *   MOTION_EXEC_WAIT     - 已进入等待状态，VM 应切换到 WAITING；
 *   MOTION_EXEC_ERROR    - 执行失败，错误已写入 vm->error；
 *   MOTION_EXEC_COMPLETE - 本段结束（ENDERROR / ENDJOB）；
 *   MOTION_EXEC_ABORT    - 中止执行。
 * 注意：返回 ERROR 时由 VM 决定是否转入 ON ERROR 处理器。
 */
motion_exec_result_t motion_executor_execute(
    motion_vm_t *vm, const motion_instruction_t *instruction);

#ifdef __cplusplus
}
#endif

#endif /* MOTION_EXECUTOR_H */
