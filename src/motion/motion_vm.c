#include "motion_vm.h"
#include "motion_device.h"
#include "motion_error.h"
#include "motion_executor.h"
#include "motion_flow.h"
#include "motion_parser.h"
#include "motion_task.h"

#include "iot.h"

#include <string.h>

/*
 * 虚拟机实现。
 *
 * 执行模型（单次 poll）：
 *   1. 若处于 WAITING，先调用 motion_vm_check_wait() 轮询条件；
 *   2. 状态仍为 RUNNING 时循环取指：seek 到 pc -> read_line -> parse ->
 *      填好 line/offset/next_offset -> motion_executor_execute()；
 *   3. 执行结果决定后续动作：WAIT 切到 WAITING 并返回；ERROR 尝试转入
 *      ON ERROR（enter_error），否则置 ERROR；COMPLETE 置 COMPLETED；
 *   4. 每次执行后调用 motion_vm_cleanup_flow() 清理已失效的 IF/LOOP 上下文；
 *   5. 单次最多执行 MOTION_MAX_EXEC_PER_POLL 条，避免长时间占用 CPU。
 *
 * 数据流：
 *   motion_source 提供按偏移 seek/read_line；解析与执行均基于当前行；
 *   跳转只修改 pc，下一次循环自然从新位置取指。
 *
 * 边界约束：
 *   调用栈/循环栈/IF 栈容量分别为 MOTION_MAX_CALL_DEPTH /
 *   MOTION_MAX_LOOP_DEPTH / MOTION_MAX_IF_DEPTH，越界由执行器报
 *   MOTION_ERR_STACK_OVERFLOW。行缓冲为 MOTION_MAX_LINE。
 */

/* 重置运行期状态：清空各栈、等待与错误，但保留 pc/source 等绑定信息 */
static void reset_runtime(motion_vm_t *vm)
{
    vm->call_depth = 0U;
    vm->loop_depth = 0U;
    vm->if_depth = 0U;
    vm->handling_error = false;
    vm->resume_state = MOTION_STATE_IDLE;
    memset(vm->call_stack, 0, sizeof(vm->call_stack));
    memset(vm->loop_stack, 0, sizeof(vm->loop_stack));
    memset(vm->if_stack, 0, sizeof(vm->if_stack));
    memset(&vm->wait, 0, sizeof(vm->wait));
    motion_variable_init(&vm->variables);
    motion_error_clear(&vm->error);
}

/* 初始化 VM 为干净的 IDLE 状态；vm 为 NULL 时直接返回 */
void motion_vm_init(motion_vm_t *vm)
{
    if (vm == NULL) {
        return;
    }

    memset(vm, 0, sizeof(*vm));
    vm->state = MOTION_STATE_IDLE;
    vm->resume_state = MOTION_STATE_IDLE;
    vm->source = NULL;
    motion_variable_init(&vm->variables);
    motion_error_clear(&vm->error);
}

/*
 * 绑定数据源并从脚本起点（偏移 0、段 0）开始。
 * 返回：0 成功；MOTION_ERR_PARAMETER 参数为空。
 */
int motion_vm_load(motion_vm_t *vm, motion_source_t *source)
{
    if (vm == NULL || source == NULL) {
        return MOTION_ERR_PARAMETER;
    }

    motion_vm_bind(vm, source, 0U, 0, false, 0U);
    return 0;
}

/*
 * 绑定数据源与执行范围，并把状态复位为 IDLE。
 * 注意：不清理运行栈与变量表（清理由 motion_vm_start 负责）。
 */
void motion_vm_bind(motion_vm_t *vm, motion_source_t *source,
                    uint32_t start_pc, int segment, bool bounded,
                    uint32_t end_offset)
{
    if (vm == NULL) {
        return;
    }

    vm->source = source;
    vm->start_pc = start_pc;
    vm->pc = start_pc;
    vm->segment = segment;
    vm->bounded = bounded;
    vm->end_offset = end_offset;
    vm->state = MOTION_STATE_IDLE;
}

/*
 * 从入口启动执行。
 * 返回：0 成功；MOTION_ERR_PARAMETER 参数为空；MOTION_ERR_FLOW seek 失败。
 * 注意：会先重置全部运行期状态与变量表；失败时状态置 ERROR。
 */
int motion_vm_start(motion_vm_t *vm)
{
    if (vm == NULL || vm->source == NULL) {
        return MOTION_ERR_PARAMETER;
    }

    reset_runtime(vm);
    vm->pc = vm->start_pc;
    /* 入口偏移可能不在行首记录中，取不到行号时退化为第 1 行 */
    vm->line = motion_flow_line_at(vm->pc);
    if (vm->line == 0U) {
        vm->line = 1U;
    }

    if (vm->source->open != NULL) {
        (void)vm->source->open(vm->source, NULL);
    }
    if (vm->source->seek != NULL &&
        vm->source->seek(vm->source, vm->pc) != 0) {
        motion_error_set(&vm->error, MOTION_ERR_FLOW, vm->line,
                         "seek failed");
        vm->state = MOTION_STATE_ERROR;
        return MOTION_ERR_FLOW;
    }

    vm->state = MOTION_STATE_RUNNING;
    return 0;
}

/* 暂停运行中的 VM：记住暂停前状态，便于 resume 精确恢复 */
int motion_vm_pause(motion_vm_t *vm)
{
    if (vm == NULL) {
        return MOTION_ERR_PARAMETER;
    }

    if (vm->state == MOTION_STATE_RUNNING ||
        vm->state == MOTION_STATE_WAITING) {
        vm->resume_state = vm->state;
        vm->state = MOTION_STATE_PAUSED;
    }
    return 0;
}

/* 恢复被暂停的 VM：回到 RUNNING 或 WAITING */
int motion_vm_resume(motion_vm_t *vm)
{
    if (vm == NULL) {
        return MOTION_ERR_PARAMETER;
    }

    if (vm->state == MOTION_STATE_PAUSED) {
        vm->state = (vm->resume_state == MOTION_STATE_WAITING)
                        ? MOTION_STATE_WAITING
                        : MOTION_STATE_RUNNING;
    }
    return 0;
}

/* 正常停止：取消未完成的等待并置 COMPLETED */
int motion_vm_stop(motion_vm_t *vm)
{
    if (vm == NULL) {
        return MOTION_ERR_PARAMETER;
    }

    vm->wait.active = false;
    vm->state = MOTION_STATE_COMPLETED;
    return 0;
}

/* 强制中止：取消等待并置 ABORTED */
int motion_vm_abort(motion_vm_t *vm)
{
    if (vm == NULL) {
        return MOTION_ERR_PARAMETER;
    }

    vm->wait.active = false;
    vm->state = MOTION_STATE_ABORTED;
    return 0;
}

/* 查询状态；vm 为 NULL 时返回 IDLE */
motion_state_t motion_vm_get_state(const motion_vm_t *vm)
{
    return (vm == NULL) ? MOTION_STATE_IDLE : vm->state;
}

/*
 * 清理流程栈：目标偏移已落到某个 IF/LOOP 块之外时，弹出该块上下文。
 * 参数：target 新的 pc 值。
 * 注意：上下文记录的区间为开区间 (块起始, 块结束之后)，边界值视为已离开。
 */
void motion_vm_cleanup_flow(motion_vm_t *vm, uint32_t target)
{
    if (vm == NULL) {
        return;
    }

    while (vm->if_depth > 0U) {
        motion_if_context_t *ctx = &vm->if_stack[vm->if_depth - 1U];

        /* 仍处于该 IF 块范围内则保留，否则弹出 */
        if (target > ctx->if_offset && target < ctx->end_next) {
            break;
        }
        vm->if_depth--;
    }

    while (vm->loop_depth > 0U) {
        motion_loop_context_t *ctx = &vm->loop_stack[vm->loop_depth - 1U];

        /* 仍处于该 LOOP 块范围内则保留，否则弹出 */
        if (target > ctx->loop_offset && target < ctx->endloop_next) {
            break;
        }
        vm->loop_depth--;
    }
}

/*
 * 跳转到指定偏移。
 * 参数：offset 目标字节偏移。
 * 说明：更新 pc 与行号（查得到时），并按目标位置清理流程栈。
 */
void motion_vm_jump(motion_vm_t *vm, uint32_t offset)
{
    uint32_t line;

    if (vm == NULL) {
        return;
    }

    vm->pc = offset;
    line = motion_flow_line_at(offset);
    if (line != 0U) {
        vm->line = line;
    }
    motion_vm_cleanup_flow(vm, offset);
}

/*
 * 记录异常并返回错误码。
 * 参数：code 错误码；message 附加消息，可为 NULL。
 * 返回：原样返回 code（vm 为 NULL 时也会返回 code）。
 */
int motion_vm_raise(motion_vm_t *vm, int code, const char *message)
{
    if (vm == NULL) {
        return code;
    }

    motion_error_set(&vm->error, code, vm->line, message);
    return code;
}

/*
 * 进入异常处理。
 * 返回：true 表示已转入 ON ERROR 处理器，false 表示无处理器并已置 ERROR。
 * 注意：handling_error 为真时不再重入，避免处理器内异常导致无限循环。
 */
static bool enter_error(motion_vm_t *vm)
{
    if (!vm->handling_error) {
        const motion_error_entry_t *handler =
            motion_flow_find_handler(vm->segment);

        if (handler != NULL && handler->active) {
            vm->handling_error = true;
            vm->wait.active = false;
            vm->state = MOTION_STATE_RUNNING;
            motion_vm_jump(vm, handler->handler_offset);
            return true;
        }
    }

    vm->wait.active = false;
    vm->state = MOTION_STATE_ERROR;
    return false;
}

/* 判断 JOB 是否已处于“结束”状态（空闲/完成/错误/中止） */
static bool job_finished(motion_state_t state)
{
    return state == MOTION_STATE_IDLE ||
           state == MOTION_STATE_COMPLETED ||
           state == MOTION_STATE_ERROR ||
           state == MOTION_STATE_ABORTED;
}

/* 按比较条件比较实际值与目标值，返回是否满足条件 */
static bool compare_wait_value(int32_t actual, motion_compare_t compare,
                               int32_t target)
{
    switch (compare) {
    case MOTION_CMP_EQ:
        return actual == target;
    case MOTION_CMP_NE:
        return actual != target;
    case MOTION_CMP_GT:
        return actual > target;
    case MOTION_CMP_GE:
        return actual >= target;
    case MOTION_CMP_LT:
        return actual < target;
    case MOTION_CMP_LE:
        return actual <= target;
    default:
        return false;
    }
}

/*
 * 轮询当前等待条件。
 * 返回：1 完成；0 继续等待；-1 错误（设备读失败 / JOB 不存在 / 超时）。
 * 注意：
 *   - 等待未激活视为立即完成；
 *   - 超时按 start_tick 起的累计 tick 计算，timeout_ms 为 0 表示不超时；
 *   - 出错时不自动转入 ON ERROR，由 motion_vm_poll 决定。
 */
int motion_vm_check_wait(motion_vm_t *vm)
{
    motion_wait_t *wait;
    uint32_t elapsed;
    bool done = false;
    int rc;

    if (vm == NULL) {
        return -1;
    }

    wait = &vm->wait;
    if (!wait->active) {
        vm->state = MOTION_STATE_RUNNING;
        return 1;
    }

    elapsed = (uint32_t)(iot_tick_get() - wait->start_tick);

    /* 按等待类型检查条件；各分支失败时写入错误并返回 -1 */
    switch (wait->type) {
    case MOTION_WAIT_TIME:
        done = (elapsed >= wait->duration_ms);
        break;

    case MOTION_WAIT_STEP: {
        bool busy = false;

        rc = motion_device_step_is_busy(wait->device_id, &busy);
        if (rc != 0) {
            motion_error_set(&vm->error, rc, vm->line, NULL);
            return -1;
        }
        done = !busy;
        break;
    }

    case MOTION_WAIT_PWM: {
        bool busy = false;

        rc = motion_device_pwm_is_busy(wait->device_id, &busy);
        if (rc != 0) {
            motion_error_set(&vm->error, rc, vm->line, NULL);
            return -1;
        }
        done = !busy;
        break;
    }

    case MOTION_WAIT_GPIO: {
        uint8_t value = 0U;

        rc = motion_device_gpio_read(wait->device_id, &value);
        if (rc != 0) {
            motion_error_set(&vm->error, rc, vm->line, NULL);
            return -1;
        }
        done = compare_wait_value((int32_t)value, wait->compare,
                                  wait->target);
        break;
    }

    case MOTION_WAIT_ADC: {
        int32_t value = 0;

        rc = motion_device_adc_read(wait->device_id, &value);
        if (rc != 0) {
            motion_error_set(&vm->error, rc, vm->line, NULL);
            return -1;
        }
        done = compare_wait_value(value, wait->compare, wait->target);
        break;
    }

    case MOTION_WAIT_SENSOR: {
        int32_t value = 0;

        rc = motion_device_sensor_read(wait->device_id, &value);
        if (rc != 0) {
            motion_error_set(&vm->error, rc, vm->line, NULL);
            return -1;
        }
        done = compare_wait_value(value, wait->compare, wait->target);
        break;
    }

    case MOTION_WAIT_ALL: {
        bool busy = false;

        rc = motion_device_any_busy(&busy);
        if (rc != 0) {
            motion_error_set(&vm->error, rc, vm->line, NULL);
            return -1;
        }
        done = !busy;
        break;
    }

    case MOTION_WAIT_JOB: {
        motion_state_t state;

        if (motion_task_find(wait->job_name) < 0) {
            motion_error_set(&vm->error, MOTION_ERR_PARAMETER, vm->line,
                             "WAIT JOB target not found");
            return -1;
        }
        state = motion_task_state(wait->job_name);
        done = job_finished(state);
        break;
    }

    default:
        done = true;
        break;
    }

    if (done) {
        wait->active = false;
        vm->state = MOTION_STATE_RUNNING;
        return 1;
    }

    /* 条件未满足时检查超时；超时以 MOTION_ERR_TIMEOUT 上报 */
    if (wait->has_timeout && wait->timeout_ms > 0U &&
        elapsed >= wait->timeout_ms) {
        wait->active = false;
        motion_error_set(&vm->error, MOTION_ERR_TIMEOUT, vm->line,
                         "wait timeout");
        return -1;
    }

    return 0;
}

/*
 * 从数据源读取当前 pc 处的一行，并输出下一行偏移。
 * 返回：true 读到有效行；false 遇到 EOF 或错误（错误码已写入 vm->error，
 *       EOF 不写错误）。
 * 注意：每次读取前都会 seek 到 pc，因此支持任意跳转后的随机访问。
 */
static bool read_source_line(motion_vm_t *vm, char *buffer, uint16_t size,
                             uint32_t *next_offset)
{
    int rc;

    if (vm->source == NULL || vm->source->read_line == NULL) {
        motion_error_set(&vm->error, MOTION_ERR_FLOW, vm->line,
                         "no source");
        return false;
    }

    if (vm->source->seek != NULL &&
        vm->source->seek(vm->source, vm->pc) != 0) {
        motion_error_set(&vm->error, MOTION_ERR_FLOW, vm->line,
                         "seek failed");
        return false;
    }

    rc = vm->source->read_line(vm->source, buffer, size);
    if (rc < 0) {
        /* -2 表示行超长（参数类错误），其余视为数据源读错误 */
        motion_error_set(&vm->error,
                         (rc == -2) ? MOTION_ERR_PARAMETER
                                    : MOTION_ERR_FLOW,
                         vm->line,
                         (rc == -2) ? "line too long" : "read failed");
        return false;
    }
    if (rc == 0) {
        return false; /* EOF，不算错误 */
    }

    if (vm->source->tell != NULL) {
        (void)vm->source->tell(vm->source, next_offset);
    } else {
        /* 数据源不支持 tell 时，退化为按行长推算 */
        *next_offset = vm->pc + (uint32_t)strlen(buffer);
    }

    return true;
}

/*
 * 调度 VM 一次（非阻塞）。
 * 流程：先处理等待（若在 WAITING），再在 RUNNING 状态下循环取指执行，
 * 直到到达单次执行上限或状态改变。
 * 注意：
 *   - 读取到 EOF 时按正常结束处理（COMPLETED），除非已有错误；
 *   - 每条指令执行后都会清理流程栈，保证跳转后上下文一致；
 *   - 解析失败记错误并尝试进入 ON ERROR，无法处理则置 ERROR。
 */
void motion_vm_poll(motion_vm_t *vm)
{
    uint8_t executed = 0U;

    if (vm == NULL) {
        return;
    }

    if (vm->state == MOTION_STATE_WAITING) {
        int wait_result = motion_vm_check_wait(vm);

        if (wait_result == 0) {
            return; /* 仍在等待，等下次轮询 */
        }
        if (wait_result < 0) {
            /* 等待出错：尝试转入异常处理器，失败则停在 ERROR */
            if (!enter_error(vm)) {
                return;
            }
        }
    }

    if (vm->state != MOTION_STATE_RUNNING) {
        return;
    }

    /* 单次 poll 限制执行条数，避免解释器长时间占用 CPU */
    while (vm->state == MOTION_STATE_RUNNING &&
           executed < MOTION_MAX_EXEC_PER_POLL) {
        char buffer[MOTION_MAX_LINE];
        motion_instruction_t ins;
        motion_parse_result_t parsed;
        motion_exec_result_t exec;
        uint32_t next_offset = vm->pc;
        uint32_t line_offset = vm->pc;
        uint32_t line;

        if (!read_source_line(vm, buffer, (uint16_t)sizeof(buffer),
                              &next_offset)) {
            if (vm->error.code != MOTION_ERR_NONE &&
                vm->error.code != 0) {
                (void)enter_error(vm); /* 读取失败：进入异常处理 */
            } else if (vm->state == MOTION_STATE_RUNNING) {
                /* 正常 EOF：处理块内结束视为出错，否则视为完成 */
                vm->state = vm->handling_error ? MOTION_STATE_ERROR
                                               : MOTION_STATE_COMPLETED;
            }
            return;
        }

        line = motion_flow_line_at(vm->pc);
        if (line != 0U) {
            vm->line = line;
        }

        parsed = motion_parser_parse(buffer, &ins);

        vm->pc = next_offset; /* 默认顺序前进；执行器可覆盖为跳转 */

        if (parsed == MOTION_PARSE_EMPTY) {
            continue; /* 空行或注释行直接跳过 */
        }

        if (parsed != MOTION_PARSE_OK) {
            int code = (parsed == MOTION_PARSE_UNSUPPORTED)
                           ? MOTION_ERR_SYNTAX
                           : MOTION_ERR_PARAMETER;

            motion_error_set(&vm->error, code, vm->line,
                             motion_parse_result_message(parsed));
            executed++;
            motion_vm_cleanup_flow(vm, vm->pc);
            if (!enter_error(vm)) {
                return;
            }
            continue;
        }

        /* 补齐指令的位置信息，供执行器用于跳转与报错 */
        ins.line = vm->line;
        ins.offset = line_offset;
        ins.next_offset = next_offset;

        exec = motion_executor_execute(vm, &ins);
        executed++;
        motion_vm_cleanup_flow(vm, vm->pc);

        if (exec == MOTION_EXEC_WAIT) {
            /* 进入等待，本条指令已完成，下次从当前 pc 继续 */
            vm->state = MOTION_STATE_WAITING;
            return;
        }
        if (exec == MOTION_EXEC_ERROR) {
            if (!enter_error(vm)) {
                return;
            }
            continue;
        }
        if (exec == MOTION_EXEC_COMPLETE) {
            /* 段执行完；处于异常处理块内则视为处理结束并报错 */
            vm->state = vm->handling_error ? MOTION_STATE_ERROR
                                           : MOTION_STATE_COMPLETED;
            vm->wait.active = false;
            return;
        }
        if (exec == MOTION_EXEC_ABORT) {
            vm->state = MOTION_STATE_ABORTED;
            return;
        }
    }
}
