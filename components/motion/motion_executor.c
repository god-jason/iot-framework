#include "motion_executor.h"
#include "motion_device.h"
#include "motion_error.h"
#include "motion_flow.h"
#include "motion_task.h"

#include "hal.h"

#include <string.h>

/*
 * 指令执行器实现。
 *
 * 执行模型：
 *   motion_vm_poll() 每取到一条指令就调用 motion_executor_execute()，
 *   本文件按 instruction->command 分发到各 execute_* 子函数。子函数内部
 *   按 action/subaction 再做一次字符串分发（STEP START / PWM DUTY 等）。
 *   - 设备类动作直接调用 motion_device_* 并把返回值转成错误码。
 *   - WAIT 类动作只填充 vm->wait 后返回 MOTION_EXEC_WAIT，真正的等待
 *     判定由 motion_vm_check_wait() 完成。
 *   - 流程类动作（IF/LOOP/GOTO/CALL...）通过修改 vm->pc 与流程栈完成跳转。
 *
 * 数据流/依赖：
 *   指令参数保存在 ins->params[] 中，可能是字面量或 $变量引用，统一经
 *   motion_value_resolve() 解析。表达式条件/赋值经 motion_flow_eval() 求值。
 *
 * 边界约束：
 *   - 目标变量名缓冲大小为 MOTION_MAX_VAR_NAME；
 *   - PWM 占空比限制在 [0, 100]；ADC 平均次数限制在 [1, MOTION_MAX_ADC_AVG]；
 *   - 流程栈深度分别受 MOTION_MAX_IF_DEPTH / MOTION_MAX_LOOP_DEPTH /
 *     MOTION_MAX_CALL_DEPTH 限制，超限报 MOTION_ERR_STACK_OVERFLOW。
 */

/* ------------------------------------------------------------------ */
/* 参数工具                                                            */
/* ------------------------------------------------------------------ */

/* 按名称在指令参数表中查找参数，未找到返回 NULL */
static const motion_param_t *find_param(const motion_instruction_t *ins,
                                        const char *name)
{
    uint8_t i;

    for (i = 0U; i < ins->param_count; i++) {
        if (strcmp(ins->params[i].name, name) == 0) {
            return &ins->params[i];
        }
    }
    return NULL;
}

/*
 * 读取并解析参数（支持 $变量）。
 * 返回：1 找到并成功解析；0 未提供该参数；-1 变量解析失败。
 * 注意：返回 0 时 *out 不会被修改。
 */
/* 1: 找到并成功解析，0: 未提供，-1: 变量解析失败 */
static int get_param(const motion_vm_t *vm, const motion_instruction_t *ins,
                     const char *name, motion_value_t *out)
{
    const motion_param_t *param = find_param(ins, name);

    if (param == NULL) {
        return 0;
    }

    if (motion_value_resolve(&param->value, &vm->variables, out) != 0) {
        return -1;
    }
    return 1;
}

/* 记录错误（含当前行号）并返回 MOTION_EXEC_ERROR */
static motion_exec_result_t exec_fail(motion_vm_t *vm, int code,
                                      const char *message)
{
    motion_error_set(&vm->error, code, vm->line, message);
    return MOTION_EXEC_ERROR;
}

/* 以 MOTION_ERR_PARAMETER 记录参数错误 */
static motion_exec_result_t param_fail(motion_vm_t *vm, const char *message)
{
    return exec_fail(vm, MOTION_ERR_PARAMETER, message);
}

/*
 * 读取整型参数。
 * 参数：def 为未提供时的默认值；out 输出解析结果。
 * 返回：1 已提供并解析；0 未提供（out = def）；-1 变量解析失败。
 */
static int param_int(const motion_vm_t *vm, const motion_instruction_t *ins,
                     const char *name, int64_t def, int64_t *out)
{
    motion_value_t value;
    int rc = get_param(vm, ins, name, &value);

    if (rc == 0) {
        *out = def;
        return 0;
    }
    if (rc < 0) {
        return -1;
    }

    *out = motion_value_to_int(&value);
    return 1;
}

/*
 * 读取带单位的浮点参数（如 SPEED 的 mm/s、step/s）。
 * 参数：def/def_unit 为未提供时的默认值与默认单位；
 *       out/unit 输出数值与单位（单位来自参数本身）。
 * 返回：1 已提供并解析；0 未提供；-1 变量解析失败。
 */
static int param_double_unit(const motion_vm_t *vm,
                             const motion_instruction_t *ins,
                             const char *name, double def,
                             motion_unit_t def_unit, double *out,
                             motion_unit_t *unit)
{
    motion_value_t value;
    int rc = get_param(vm, ins, name, &value);

    if (rc == 0) {
        *out = def;
        *unit = def_unit;
        return 0;
    }
    if (rc < 0) {
        return -1;
    }

    *out = motion_value_to_double(&value);
    *unit = value.unit;
    return 1;
}

/*
 * 读取 TIMEOUT 参数（毫秒）。
 * 返回：0 成功（未提供时使用 MOTION_DEFAULT_WAIT_TIMEOUT）；-1 变量解析失败。
 * 注意：显式给出的非正数会被裁剪为 0，表示不超时。
 */
static int param_timeout(const motion_vm_t *vm,
                         const motion_instruction_t *ins,
                         uint32_t *timeout, bool *has_timeout)
{
    motion_value_t value;
    int rc = get_param(vm, ins, "TIMEOUT", &value);

    *timeout = (uint32_t)MOTION_DEFAULT_WAIT_TIMEOUT;
    *has_timeout = (MOTION_DEFAULT_WAIT_TIMEOUT > 0);

    if (rc < 0) {
        return -1;
    }
    if (rc > 0) {
        int64_t ms = motion_value_to_int(&value);

        *timeout = (ms > 0) ? (uint32_t)ms : 0U;
        *has_timeout = true;
    }
    return 0;
}

/*
 * 把参数中的变量引用名复制到 name 缓冲（如 TO $pos）。
 * 返回：1 成功；0 参数未提供；-1 参数存在但不是变量引用。
 * 注意：name 最多写入 size-1 个字符并保证以 '\0' 结尾。
 */
static int copy_to_variable(const motion_instruction_t *ins,
                            const char *param, char *name, size_t size)
{
    const motion_param_t *p = find_param(ins, param);

    if (p == NULL) {
        return 0;
    }
    if (p->value.type != MOTION_VALUE_VARIABLE) {
        return -1;
    }

    strncpy(name, p->value.data.variable, size - 1U);
    name[size - 1U] = '\0';
    return 1;
}

/*
 * 对表达式求值，失败时把错误码写入 vm->error。
 * 返回：0 成功；-1 失败（vm->error 已更新）。
 */
static int eval_expression(motion_vm_t *vm, const char *expression,
                           motion_value_t *value)
{
    int rc = motion_flow_eval(expression, &vm->variables, value);

    if (rc != 0) {
        int code = -rc;

        if (code <= 0) {
            code = MOTION_ERR_SYNTAX;
        }
        motion_error_set(&vm->error, code, vm->line, NULL);
        return -1;
    }
    return 0;
}

/*
 * 从指令中提取比较条件（GT/GE/LT/LE/NE/EQ/VALUE，VALUE 等价于 EQ）。
 * 返回：1 找到条件；0 没有条件参数；-1 变量解析失败。
 * 注意：names 与 compares 按下标一一对应，顺序不能随意调整。
 */
/* 1 成功，0 没有条件参数，-1 变量解析失败 */
static int get_condition(const motion_vm_t *vm,
                         const motion_instruction_t *ins,
                         motion_compare_t *compare, int32_t *target)
{
    static const char *const names[] = {
        "GT", "GE", "LT", "LE", "NE", "EQ", "VALUE"
    };
    static const motion_compare_t compares[] = {
        MOTION_CMP_GT, MOTION_CMP_GE, MOTION_CMP_LT, MOTION_CMP_LE,
        MOTION_CMP_NE, MOTION_CMP_EQ, MOTION_CMP_EQ
    };
    size_t i;

    for (i = 0U; i < sizeof(names) / sizeof(names[0]); i++) {
        if (find_param(ins, names[i]) != NULL) {
            motion_value_t value;
            int rc = get_param(vm, ins, names[i], &value);

            if (rc <= 0) {
                return (rc < 0) ? -1 : 0;
            }
            *compare = compares[i];
            *target = (int32_t)motion_value_to_int(&value);
            return 1;
        }
    }

    return 0;
}

/* 初始化等待上下文：清零、设置类型、记录起始 tick 并置 active */
static void begin_wait(motion_vm_t *vm, motion_wait_type_t type)
{
    memset(&vm->wait, 0, sizeof(vm->wait));
    vm->wait.type = type;
    vm->wait.active = true;
    vm->wait.start_tick = hal_tick_get();
}

/* ------------------------------------------------------------------ */
/* STEP / PWM                                                          */
/* ------------------------------------------------------------------ */

/*
 * 执行 STEP 指令，按 action 分发：
 *   START    - 启动运动（支持 TIME 定时或 PULSE 定脉冲，二者互斥）；
 *   STOP     - 按减速参数停止；
 *   SPEED    - 运行中修改速度；
 *   DIR      - 修改方向；
 *   ENABLE   - 使能驱动器；DISABLE - 关闭使能；
 *   HOME     - 回零；
 *   ZERO     - 位置清零；
 *   POSITION READ/SET - 读取/设置位置。
 * 所有参数通过 get_param/param_* 解析，缺失时使用默认值。
 * 返回 MOTION_EXEC_OK 或 MOTION_EXEC_ERROR（错误写入 vm->error）。
 */
static motion_exec_result_t execute_step(motion_vm_t *vm,
                                         const motion_instruction_t *ins)
{
    if (strcmp(ins->action, "START") == 0) {
        motion_value_t dir_value;
        double speed = 0.0;
        motion_unit_t unit = MOTION_UNIT_STEP_S;
        int64_t time_ms = 0;
        int64_t pulse = 0;
        int64_t acc = 0;
        int64_t dec = 0;
        int has_time;
        int has_pulse;
        int rc;

        rc = get_param(vm, ins, "DIR", &dir_value);
        if (rc < 0) {
            return exec_fail(vm, MOTION_ERR_VARIABLE, NULL);
        }
        if (rc == 0) {
            /* 未提供 DIR 时默认 0（由设备层解释为正转） */
            memset(&dir_value, 0, sizeof(dir_value));
            dir_value.type = MOTION_VALUE_INTEGER;
        }

        has_time = param_int(vm, ins, "TIME", 0, &time_ms);
        has_pulse = param_int(vm, ins, "PULSE", 0, &pulse);
        if (has_time < 0 || has_pulse < 0) {
            return exec_fail(vm, MOTION_ERR_VARIABLE, NULL);
        }
        if (has_time > 0 && has_pulse > 0) {
            /* 定时与定脉冲是两种互斥的运动方式 */
            return param_fail(vm, "TIME and PULSE can not be used together");
        }

        if (param_double_unit(vm, ins, "SPEED", 0.0, MOTION_UNIT_STEP_S,
                              &speed, &unit) < 0) {
            return exec_fail(vm, MOTION_ERR_VARIABLE, NULL);
        }
        if (param_int(vm, ins, "ACC", 0, &acc) < 0 ||
            param_int(vm, ins, "DEC", 0, &dec) < 0) {
            return exec_fail(vm, MOTION_ERR_VARIABLE, NULL);
        }

        rc = motion_device_step_start(
            ins->device_id,
            (uint8_t)(motion_value_to_int(&dir_value) & 1), /* 方向仅取最低位 */
            speed, unit, (int32_t)pulse, (uint32_t)time_ms,
            (uint32_t)acc, (uint32_t)dec);
        if (rc != 0) {
            return exec_fail(vm, rc, "STEP start failed");
        }
        return MOTION_EXEC_OK;
    }

    if (strcmp(ins->action, "STOP") == 0) {
        int64_t dec = 0;
        int rc;

        if (param_int(vm, ins, "DEC", 0, &dec) < 0) {
            return exec_fail(vm, MOTION_ERR_VARIABLE, NULL);
        }
        rc = motion_device_step_stop(ins->device_id, (uint32_t)dec);
        if (rc != 0) {
            return exec_fail(vm, rc, "STEP stop failed");
        }
        return MOTION_EXEC_OK;
    }

    if (strcmp(ins->action, "SPEED") == 0) {
        motion_value_t value;
        int rc;

        if (get_param(vm, ins, "VALUE", &value) <= 0) {
            return param_fail(vm, "STEP SPEED requires a value");
        }
        rc = motion_device_step_set_speed(ins->device_id,
                                          motion_value_to_double(&value),
                                          value.unit);
        if (rc != 0) {
            return exec_fail(vm, rc, "STEP set speed failed");
        }
        return MOTION_EXEC_OK;
    }

    if (strcmp(ins->action, "DIR") == 0) {
        motion_value_t value;
        int rc;

        if (get_param(vm, ins, "VALUE", &value) <= 0) {
            return param_fail(vm, "STEP DIR requires a value");
        }
        rc = motion_device_step_set_dir(
            ins->device_id, (uint8_t)(motion_value_to_int(&value) & 1));
        if (rc != 0) {
            return exec_fail(vm, rc, "STEP set dir failed");
        }
        return MOTION_EXEC_OK;
    }

    if (strcmp(ins->action, "ENABLE") == 0 ||
        strcmp(ins->action, "DISABLE") == 0) {
        int rc = motion_device_step_enable(
            ins->device_id,
            (uint8_t)(strcmp(ins->action, "ENABLE") == 0 ? 1 : 0));

        if (rc != 0) {
            return exec_fail(vm, rc, "STEP enable/disable failed");
        }
        return MOTION_EXEC_OK;
    }

    if (strcmp(ins->action, "HOME") == 0) {
        motion_value_t dir_value;
        double speed = 0.0;
        motion_unit_t unit = MOTION_UNIT_STEP_S;
        int64_t timeout = 0;
        int rc;

        rc = get_param(vm, ins, "DIR", &dir_value);
        if (rc < 0) {
            return exec_fail(vm, MOTION_ERR_VARIABLE, NULL);
        }
        if (rc == 0) {
            memset(&dir_value, 0, sizeof(dir_value));
            dir_value.type = MOTION_VALUE_INTEGER;
        }
        if (param_double_unit(vm, ins, "SPEED", 0.0, MOTION_UNIT_STEP_S,
                              &speed, &unit) < 0 ||
            param_int(vm, ins, "TIMEOUT", 0, &timeout) < 0) {
            return exec_fail(vm, MOTION_ERR_VARIABLE, NULL);
        }

        rc = motion_device_step_home(
            ins->device_id, speed, unit,
            (uint8_t)(motion_value_to_int(&dir_value) & 1),
            (uint32_t)timeout);
        if (rc != 0) {
            return exec_fail(vm, rc, "STEP home failed");
        }
        return MOTION_EXEC_OK;
    }

    if (strcmp(ins->action, "ZERO") == 0) {
        int rc = motion_device_step_position_set(ins->device_id, 0);

        if (rc != 0) {
            return exec_fail(vm, rc, "STEP zero failed");
        }
        return MOTION_EXEC_OK;
    }

    if (strcmp(ins->action, "POSITION") == 0) {
        if (strcmp(ins->subaction, "READ") == 0) {
            char target[MOTION_MAX_VAR_NAME];
            int32_t position = 0;
            int rc;
            int has_target;

            rc = motion_device_step_position_read(ins->device_id, &position);
            if (rc != 0) {
                return exec_fail(vm, rc, "STEP position read failed");
            }

            has_target = copy_to_variable(ins, "TO", target, sizeof(target));
            if (has_target < 0) {
                return param_fail(vm, "TO requires a variable");
            }
            if (has_target > 0) {
                motion_value_t value;

                memset(&value, 0, sizeof(value));
                value.type = MOTION_VALUE_INTEGER;
                value.data.integer = position;
                if (motion_variable_set(&vm->variables, target, &value) != 0) {
                    return exec_fail(vm, MOTION_ERR_VARIABLE,
                                     "too many variables");
                }
            }
            return MOTION_EXEC_OK;
        }

        if (strcmp(ins->subaction, "SET") == 0) {
            motion_value_t value;
            int rc;

            if (get_param(vm, ins, "VALUE", &value) <= 0) {
                return param_fail(vm, "STEP POSITION SET requires a value");
            }
            rc = motion_device_step_position_set(
                ins->device_id, (int32_t)motion_value_to_int(&value));
            if (rc != 0) {
                return exec_fail(vm, rc, "STEP position set failed");
            }
            return MOTION_EXEC_OK;
        }

        return param_fail(vm, "STEP POSITION requires READ or SET");
    }

    return param_fail(vm, "unsupported STEP action");
}

/*
 * 执行 PWM 指令，按 action 分发：
 *   START - 以 FREQ/DUTY/ACCEL 启动，频率非负、占空比裁剪到 [0,100]；
 *   DUTY  - 修改占空比（同样裁剪到 [0,100]）；
 *   FREQ  - 修改频率；
 *   STOP  - 按 DECEL 减速停止。
 * 返回 MOTION_EXEC_OK 或 MOTION_EXEC_ERROR。
 */
static motion_exec_result_t execute_pwm(motion_vm_t *vm,
                                        const motion_instruction_t *ins)
{
    if (strcmp(ins->action, "START") == 0) {
        motion_value_t value;
        int64_t freq = 0;
        int64_t duty = 0;
        int64_t accel = 0;
        int rc;
        int has_value;

        has_value = get_param(vm, ins, "FREQ", &value);
        if (has_value < 0) {
            return exec_fail(vm, MOTION_ERR_VARIABLE, NULL);
        }
        if (has_value > 0) {
            freq = motion_value_to_int(&value);
        }

        has_value = get_param(vm, ins, "DUTY", &value);
        if (has_value < 0) {
            return exec_fail(vm, MOTION_ERR_VARIABLE, NULL);
        }
        if (has_value > 0) {
            duty = motion_value_to_int(&value);
        }
        if (param_int(vm, ins, "ACCEL", 0, &accel) < 0) {
            return exec_fail(vm, MOTION_ERR_VARIABLE, NULL);
        }

        if (duty < 0) {
            duty = 0; /* 占空比下限裁剪 */
        } else if (duty > 100) {
            duty = 100; /* 占空比上限裁剪 */
        }
        if (freq < 0) {
            freq = 0; /* 频率不允许为负 */
        }

        rc = motion_device_pwm_start(ins->device_id, (uint32_t)freq,
                                     (uint16_t)duty, (uint32_t)accel);
        if (rc != 0) {
            return exec_fail(vm, rc, "PWM start failed");
        }
        return MOTION_EXEC_OK;
    }

    if (strcmp(ins->action, "DUTY") == 0) {
        motion_value_t value;
        int64_t duty;
        int rc;

        if (get_param(vm, ins, "VALUE", &value) <= 0) {
            return param_fail(vm, "PWM DUTY requires a value");
        }
        duty = motion_value_to_int(&value);
        if (duty < 0) {
            duty = 0; /* 占空比裁剪到 [0,100] */
        } else if (duty > 100) {
            duty = 100;
        }

        rc = motion_device_pwm_set_duty(ins->device_id, (uint16_t)duty);
        if (rc != 0) {
            return exec_fail(vm, rc, "PWM set duty failed");
        }
        return MOTION_EXEC_OK;
    }

    if (strcmp(ins->action, "FREQ") == 0) {
        motion_value_t value;
        int rc;

        if (get_param(vm, ins, "VALUE", &value) <= 0) {
            return param_fail(vm, "PWM FREQ requires a value");
        }
        rc = motion_device_pwm_set_freq(
            ins->device_id, (uint32_t)motion_value_to_int(&value));
        if (rc != 0) {
            return exec_fail(vm, rc, "PWM set freq failed");
        }
        return MOTION_EXEC_OK;
    }

    if (strcmp(ins->action, "STOP") == 0) {
        int64_t decel = 0;
        int rc;

        if (param_int(vm, ins, "DECEL", 0, &decel) < 0) {
            return exec_fail(vm, MOTION_ERR_VARIABLE, NULL);
        }
        rc = motion_device_pwm_stop(ins->device_id, (uint32_t)decel);
        if (rc != 0) {
            return exec_fail(vm, rc, "PWM stop failed");
        }
        return MOTION_EXEC_OK;
    }

    return param_fail(vm, "unsupported PWM action");
}

/* ------------------------------------------------------------------ */
/* GPIO / RELAY / ADC / SENSOR                                         */
/* ------------------------------------------------------------------ */

/*
 * 执行 GPIO 指令：ON/OFF 写固定电平，SET 写 VALUE 的逻辑值，
 * READ 读取电平并可写入 TO 指定的变量。
 * 返回 MOTION_EXEC_OK 或 MOTION_EXEC_ERROR。
 */
static motion_exec_result_t execute_gpio(motion_vm_t *vm,
                                         const motion_instruction_t *ins)
{
    if (strcmp(ins->action, "ON") == 0 ||
        strcmp(ins->action, "OFF") == 0) {
        int rc = motion_device_gpio_write(
            ins->device_id,
            (uint8_t)(strcmp(ins->action, "ON") == 0 ? 1 : 0));

        return (rc == 0) ? MOTION_EXEC_OK
                         : exec_fail(vm, rc, "GPIO write failed");
    }

    if (strcmp(ins->action, "SET") == 0) {
        motion_value_t value;
        int rc;

        if (get_param(vm, ins, "VALUE", &value) <= 0) {
            return param_fail(vm, "GPIO SET requires VALUE");
        }
        rc = motion_device_gpio_write(
            ins->device_id,
            (uint8_t)(motion_value_to_int(&value) != 0 ? 1 : 0));
        return (rc == 0) ? MOTION_EXEC_OK
                         : exec_fail(vm, rc, "GPIO write failed");
    }

    if (strcmp(ins->action, "READ") == 0) {
        char target[MOTION_MAX_VAR_NAME];
        uint8_t value = 0U;
        int rc = motion_device_gpio_read(ins->device_id, &value);
        int has_target;

        if (rc != 0) {
            return exec_fail(vm, rc, "GPIO read failed");
        }

        has_target = copy_to_variable(ins, "TO", target, sizeof(target));
        if (has_target < 0) {
            return param_fail(vm, "TO requires a variable");
        }
        if (has_target > 0) {
            motion_value_t result;

            memset(&result, 0, sizeof(result));
            result.type = MOTION_VALUE_INTEGER;
            result.data.integer = value;
            if (motion_variable_set(&vm->variables, target, &result) != 0) {
                return exec_fail(vm, MOTION_ERR_VARIABLE,
                                 "too many variables");
            }
        }
        return MOTION_EXEC_OK;
    }

    return param_fail(vm, "unsupported GPIO action");
}

/*
 * 执行 RELAY 指令：ON/OFF 写继电器状态，READ 读取状态并可写入 TO 变量。
 * 返回 MOTION_EXEC_OK 或 MOTION_EXEC_ERROR。
 */
static motion_exec_result_t execute_relay(motion_vm_t *vm,
                                          const motion_instruction_t *ins)
{
    if (strcmp(ins->action, "ON") == 0 ||
        strcmp(ins->action, "OFF") == 0) {
        int rc = motion_device_relay_write(
            ins->device_id,
            (uint8_t)(strcmp(ins->action, "ON") == 0 ? 1 : 0));

        return (rc == 0) ? MOTION_EXEC_OK
                         : exec_fail(vm, rc, "RELAY write failed");
    }

    if (strcmp(ins->action, "READ") == 0) {
        char target[MOTION_MAX_VAR_NAME];
        uint8_t value = 0U;
        int rc = motion_device_relay_read(ins->device_id, &value);
        int has_target;

        if (rc != 0) {
            return exec_fail(vm, rc, "RELAY read failed");
        }

        has_target = copy_to_variable(ins, "TO", target, sizeof(target));
        if (has_target < 0) {
            return param_fail(vm, "TO requires a variable");
        }
        if (has_target > 0) {
            motion_value_t result;

            memset(&result, 0, sizeof(result));
            result.type = MOTION_VALUE_INTEGER;
            result.data.integer = value;
            if (motion_variable_set(&vm->variables, target, &result) != 0) {
                return exec_fail(vm, MOTION_ERR_VARIABLE,
                                 "too many variables");
            }
        }
        return MOTION_EXEC_OK;
    }

    return param_fail(vm, "unsupported RELAY action");
}

/*
 * 执行 ADC 指令：
 *   READ - 连续采样 AVG 次（1..MOTION_MAX_ADC_AVG）取平均值，
 *          结果可写入 TO 变量；
 *   WAIT - 设置 ADC 等待条件与超时，返回 MOTION_EXEC_WAIT。
 * 返回 MOTION_EXEC_OK / MOTION_EXEC_WAIT / MOTION_EXEC_ERROR。
 */
static motion_exec_result_t execute_adc(motion_vm_t *vm,
                                        const motion_instruction_t *ins)
{
    if (strcmp(ins->action, "READ") == 0) {
        int64_t average = 1;
        int64_t i;
        int64_t sum = 0;
        int32_t value = 0;
        char target[MOTION_MAX_VAR_NAME];
        int has_target;

        if (param_int(vm, ins, "AVG", 1, &average) < 0) {
            return exec_fail(vm, MOTION_ERR_VARIABLE, NULL);
        }
        if (average < 1) {
            average = 1; /* 至少采样一次 */
        } else if (average > MOTION_MAX_ADC_AVG) {
            average = MOTION_MAX_ADC_AVG; /* 限制单次阻塞采样次数 */
        }

        for (i = 0; i < average; i++) {
            int rc = motion_device_adc_read(ins->device_id, &value);

            if (rc != 0) {
                return exec_fail(vm, rc, "ADC read failed");
            }
            sum += value;
        }

        has_target = copy_to_variable(ins, "TO", target, sizeof(target));
        if (has_target < 0) {
            return param_fail(vm, "TO requires a variable");
        }
        if (has_target > 0) {
            motion_value_t result;

            memset(&result, 0, sizeof(result));
            result.type = MOTION_VALUE_INTEGER;
            result.data.integer = sum / average;
            if (motion_variable_set(&vm->variables, target, &result) != 0) {
                return exec_fail(vm, MOTION_ERR_VARIABLE,
                                 "too many variables");
            }
        }
        return MOTION_EXEC_OK;
    }

    if (strcmp(ins->action, "WAIT") == 0) {
        motion_compare_t compare = MOTION_CMP_NONE;
        int32_t target = 0;
        int rc = get_condition(vm, ins, &compare, &target);

        if (rc <= 0) {
            return (rc < 0)
                       ? exec_fail(vm, MOTION_ERR_VARIABLE, NULL)
                       : param_fail(vm, "ADC WAIT requires a condition");
        }

        begin_wait(vm, MOTION_WAIT_ADC);
        vm->wait.device_id = ins->device_id;
        vm->wait.compare = compare;
        vm->wait.target = target;
        if (param_timeout(vm, ins, &vm->wait.timeout_ms,
                          &vm->wait.has_timeout) < 0) {
            return exec_fail(vm, MOTION_ERR_VARIABLE, NULL);
        }
        return MOTION_EXEC_WAIT;
    }

    return param_fail(vm, "unsupported ADC action");
}

/*
 * 执行 SENSOR 指令：
 *   READ - 读传感器值，可写入 TO 变量；
 *   WAIT - 设置比较条件与超时，返回 MOTION_EXEC_WAIT。
 * 返回 MOTION_EXEC_OK / MOTION_EXEC_WAIT / MOTION_EXEC_ERROR。
 */
static motion_exec_result_t execute_sensor(motion_vm_t *vm,
                                           const motion_instruction_t *ins)
{
    if (strcmp(ins->action, "READ") == 0) {
        int32_t value = 0;
        char target[MOTION_MAX_VAR_NAME];
        int rc = motion_device_sensor_read(ins->device_id, &value);
        int has_target;

        if (rc != 0) {
            return exec_fail(vm, rc, "SENSOR read failed");
        }

        has_target = copy_to_variable(ins, "TO", target, sizeof(target));
        if (has_target < 0) {
            return param_fail(vm, "TO requires a variable");
        }
        if (has_target > 0) {
            motion_value_t result;

            memset(&result, 0, sizeof(result));
            result.type = MOTION_VALUE_INTEGER;
            result.data.integer = value;
            if (motion_variable_set(&vm->variables, target, &result) != 0) {
                return exec_fail(vm, MOTION_ERR_VARIABLE,
                                 "too many variables");
            }
        }
        return MOTION_EXEC_OK;
    }

    if (strcmp(ins->action, "WAIT") == 0) {
        motion_compare_t compare = MOTION_CMP_NONE;
        int32_t target = 0;
        int rc = get_condition(vm, ins, &compare, &target);

        if (rc <= 0) {
            return (rc < 0)
                       ? exec_fail(vm, MOTION_ERR_VARIABLE, NULL)
                       : param_fail(vm, "SENSOR WAIT requires VALUE");
        }

        begin_wait(vm, MOTION_WAIT_SENSOR);
        vm->wait.device_id = ins->device_id;
        vm->wait.compare = compare;
        vm->wait.target = target;
        if (param_timeout(vm, ins, &vm->wait.timeout_ms,
                          &vm->wait.has_timeout) < 0) {
            return exec_fail(vm, MOTION_ERR_VARIABLE, NULL);
        }
        return MOTION_EXEC_WAIT;
    }

    return param_fail(vm, "unsupported SENSOR action");
}

/* ------------------------------------------------------------------ */
/* WAIT                                                                */
/* ------------------------------------------------------------------ */

/*
 * 执行 WAIT 指令：只负责构造等待条件，不在此处阻塞。
 *   TIME   - 按毫秒定时（非正值按 0 处理）；
 *   STEP/PWM - 等待指定设备空闲；
 *   GPIO/SENSOR - 等待读值等于 VALUE；
 *   ALL    - 等待所有设备空闲；
 *   JOB    - 等待指定 JOB 结束（启动前会校验 JOB 是否存在）。
 * 返回 MOTION_EXEC_WAIT；参数错误时返回 MOTION_EXEC_ERROR。
 */
static motion_exec_result_t execute_wait(motion_vm_t *vm,
                                         const motion_instruction_t *ins)
{
    if (strcmp(ins->action, "TIME") == 0) {
        motion_value_t value;

        if (get_param(vm, ins, "VALUE", &value) <= 0) {
            return param_fail(vm, "WAIT TIME requires a value");
        }

        begin_wait(vm, MOTION_WAIT_TIME);
        {
            int64_t ms = motion_value_to_int(&value);

            vm->wait.duration_ms = (ms > 0) ? (uint32_t)ms : 0U; /* 负值按 0 处理 */
        }
        return MOTION_EXEC_WAIT;
    }

    if (strcmp(ins->action, "STEP") == 0 ||
        strcmp(ins->action, "PWM") == 0) {
        begin_wait(vm, (strcmp(ins->action, "STEP") == 0)
                           ? MOTION_WAIT_STEP
                           : MOTION_WAIT_PWM);
        vm->wait.device_id = ins->device_id;
        if (param_timeout(vm, ins, &vm->wait.timeout_ms,
                          &vm->wait.has_timeout) < 0) {
            return exec_fail(vm, MOTION_ERR_VARIABLE, NULL);
        }
        return MOTION_EXEC_WAIT;
    }

    if (strcmp(ins->action, "GPIO") == 0 ||
        strcmp(ins->action, "SENSOR") == 0) {
        motion_value_t value;

        if (get_param(vm, ins, "VALUE", &value) <= 0) {
            return param_fail(vm, "WAIT requires VALUE");
        }

        begin_wait(vm, (strcmp(ins->action, "GPIO") == 0)
                           ? MOTION_WAIT_GPIO
                           : MOTION_WAIT_SENSOR);
        vm->wait.device_id = ins->device_id;
        vm->wait.compare = MOTION_CMP_EQ;
        vm->wait.target = (int32_t)motion_value_to_int(&value);
        if (param_timeout(vm, ins, &vm->wait.timeout_ms,
                          &vm->wait.has_timeout) < 0) {
            return exec_fail(vm, MOTION_ERR_VARIABLE, NULL);
        }
        return MOTION_EXEC_WAIT;
    }

    if (strcmp(ins->action, "ALL") == 0) {
        begin_wait(vm, MOTION_WAIT_ALL);
        if (param_timeout(vm, ins, &vm->wait.timeout_ms,
                          &vm->wait.has_timeout) < 0) {
            return exec_fail(vm, MOTION_ERR_VARIABLE, NULL);
        }
        return MOTION_EXEC_WAIT;
    }

    if (strcmp(ins->action, "JOB") == 0) {
        if (motion_task_find(ins->name) < 0) {
            return param_fail(vm, "WAIT JOB target not found");
        }

        begin_wait(vm, MOTION_WAIT_JOB);
        strncpy(vm->wait.job_name, ins->name,
                sizeof(vm->wait.job_name) - 1U);
        if (param_timeout(vm, ins, &vm->wait.timeout_ms,
                          &vm->wait.has_timeout) < 0) {
            return exec_fail(vm, MOTION_ERR_VARIABLE, NULL);
        }
        return MOTION_EXEC_WAIT;
    }

    return param_fail(vm, "unsupported WAIT target");
}

/* ------------------------------------------------------------------ */
/* 流程控制                                                            */
/* ------------------------------------------------------------------ */

/*
 * 执行流程控制指令（IF/ELSE/END/LOOP/ENDLOOP/BREAK/LABEL/GOTO/CALL/RETURN）。
 * 通过读写 vm->if_stack / loop_stack / call_stack 与 vm->pc 实现跳转，
 * 所有配对信息来自 motion_flow 在加载期建立的索引。
 * 返回 MOTION_EXEC_OK，结构或栈错误时返回 MOTION_EXEC_ERROR。
 */
static motion_exec_result_t execute_flow(motion_vm_t *vm,
                                         const motion_instruction_t *ins)
{
    if (ins->command == MOTION_CMD_IF) {
        motion_value_t value;
        const motion_if_entry_t *entry;

        if (eval_expression(vm, ins->expression, &value) != 0) {
            return MOTION_EXEC_ERROR;
        }

        entry = motion_flow_find_if(ins->offset);
        if (entry == NULL) {
            return exec_fail(vm, MOTION_ERR_FLOW, "IF without matching END");
        }

        if (motion_value_to_bool(&value)) {
            /* 条件成立：压入 IF 上下文，供后续 ELSE/END 判断是否越界 */
            if (vm->if_depth >= MOTION_MAX_IF_DEPTH) {
                return exec_fail(vm, MOTION_ERR_STACK_OVERFLOW,
                                 "IF nesting too deep");
            }
            vm->if_stack[vm->if_depth].if_offset = ins->offset;
            vm->if_stack[vm->if_depth].end_next = entry->end_next;
            vm->if_depth++;
        } else {
            /* 条件不成立：有 ELSE 跳到 ELSE 分支，否则跳到 END 之后 */
            motion_vm_jump(vm, entry->has_else ? entry->else_next
                                               : entry->end_next);
        }
        return MOTION_EXEC_OK;
    }

    if (ins->command == MOTION_CMD_ELSE) {
        const motion_if_entry_t *entry = motion_flow_find_else(ins->offset);

        if (entry == NULL) {
            return exec_fail(vm, MOTION_ERR_FLOW,
                             "ELSE without matching IF");
        }
        /* THEN 分支执行完落到 ELSE，必须跳过 ELSE 分支 */
        motion_vm_jump(vm, entry->end_next);
        return MOTION_EXEC_OK;
    }

    if (ins->command == MOTION_CMD_END) {
        return MOTION_EXEC_OK;
    }

    if (ins->command == MOTION_CMD_LOOP) {
        const motion_loop_entry_t *entry =
            motion_flow_find_loop(ins->offset);
        int64_t total = 0;
        bool infinite = (ins->expression[0] == '\0'); /* 无次数参数表示无限循环 */

        if (entry == NULL) {
            return exec_fail(vm, MOTION_ERR_FLOW,
                             "LOOP without matching ENDLOOP");
        }

        if (!infinite) {
            motion_value_t value;

            if (eval_expression(vm, ins->expression, &value) != 0) {
                return MOTION_EXEC_ERROR;
            }
            total = motion_value_to_int(&value);
            if (total <= 0) {
                /* 循环次数非正：直接跳过整个循环体 */
                motion_vm_jump(vm, entry->endloop_next);
                return MOTION_EXEC_OK;
            }
        }

        if (vm->loop_depth >= MOTION_MAX_LOOP_DEPTH) {
            return exec_fail(vm, MOTION_ERR_STACK_OVERFLOW,
                             "LOOP nesting too deep");
        }

        /* 压入循环上下文：记录循环体、结束位置与总次数 */
        vm->loop_stack[vm->loop_depth].body_offset = entry->body_offset;
        vm->loop_stack[vm->loop_depth].endloop_next =
            entry->endloop_next;
        vm->loop_stack[vm->loop_depth].loop_offset = ins->offset;
        vm->loop_stack[vm->loop_depth].total = total;
        vm->loop_stack[vm->loop_depth].current = 0;
        vm->loop_stack[vm->loop_depth].infinite = infinite;
        vm->loop_depth++;
        return MOTION_EXEC_OK;
    }

    if (ins->command == MOTION_CMD_ENDLOOP) {
        motion_loop_context_t *ctx;

        if (vm->loop_depth == 0U) {
            return exec_fail(vm, MOTION_ERR_FLOW,
                             "ENDLOOP without matching LOOP");
        }

        ctx = &vm->loop_stack[vm->loop_depth - 1U];
        ctx->current++;

        if (ctx->infinite || ctx->current < ctx->total) {
            motion_vm_jump(vm, ctx->body_offset); /* 还有剩余次数，回到循环体 */
        } else {
            motion_vm_jump(vm, ctx->endloop_next); /* 次数用尽，退出循环 */
        }
        return MOTION_EXEC_OK;
    }

    if (ins->command == MOTION_CMD_BREAK) {
        if (vm->loop_depth == 0U) {
            return exec_fail(vm, MOTION_ERR_FLOW, "BREAK outside LOOP");
        }
        /* BREAK 直接跳到最内层循环的 ENDLOOP 之后 */
        motion_vm_jump(vm, vm->loop_stack[vm->loop_depth - 1U].endloop_next);
        return MOTION_EXEC_OK;
    }

    if (ins->command == MOTION_CMD_LABEL) {
        return MOTION_EXEC_OK; /* 标签本身不产生动作 */
    }

    if (ins->command == MOTION_CMD_GOTO) {
        const motion_label_t *label =
            motion_flow_find_label(ins->name, vm->segment);

        if (label == NULL) {
            return exec_fail(vm, MOTION_ERR_FLOW, "label not found");
        }
        motion_vm_jump(vm, label->next_offset); /* 跳到标签下一行 */
        return MOTION_EXEC_OK;
    }

    if (ins->command == MOTION_CMD_CALL) {
        const motion_label_t *label =
            motion_flow_find_label(ins->name, vm->segment);

        if (label == NULL) {
            return exec_fail(vm, MOTION_ERR_FLOW, "label not found");
        }
        if (vm->call_depth >= MOTION_MAX_CALL_DEPTH) {
            return exec_fail(vm, MOTION_ERR_STACK_OVERFLOW,
                             "CALL depth exceeded");
        }
        /* 保存返回地址（CALL 的下一行）再跳转 */
        vm->call_stack[vm->call_depth++] = ins->next_offset;
        motion_vm_jump(vm, label->next_offset);
        return MOTION_EXEC_OK;
    }

    if (ins->command == MOTION_CMD_RETURN) {
        if (vm->call_depth == 0U) {
            return exec_fail(vm, MOTION_ERR_FLOW,
                             "RETURN without CALL");
        }
        vm->call_depth--;
        motion_vm_jump(vm, vm->call_stack[vm->call_depth]); /* 返回 CALL 之后 */
        return MOTION_EXEC_OK;
    }

    return exec_fail(vm, MOTION_ERR_UNSUPPORTED, NULL);
}

/* ------------------------------------------------------------------ */
/* 异常与任务                                                          */
/* ------------------------------------------------------------------ */

/*
 * 执行 ERROR 指令：主动抛出异常。
 * CODE 优先作为错误码；否则若提供 MSG 则以消息形式记录（运行时错误）；
 * 两者都没有时使用 MOTION_ERR_RUNTIME。
 * 返回 MOTION_EXEC_ERROR，由 VM 决定是否转入 ON ERROR 处理器。
 */
static motion_exec_result_t execute_error(motion_vm_t *vm,
                                          const motion_instruction_t *ins)
{
    motion_value_t value;
    int code = MOTION_ERR_RUNTIME;

    if (get_param(vm, ins, "CODE", &value) > 0) {
        code = (int)motion_value_to_int(&value);
    } else if (get_param(vm, ins, "MSG", &value) > 0) {
        motion_error_set(&vm->error, MOTION_ERR_RUNTIME, vm->line,
                         value.data.string);
        return MOTION_EXEC_ERROR;
    }

    motion_error_set(&vm->error, code, vm->line, NULL);
    return MOTION_EXEC_ERROR;
}

/*
 * 执行 JOB 相关指令：
 *   START_JOB - 启动指定 JOB；
 *   STOP_JOB  - 停止指定 JOB；
 *   JOB_STATE - 查询 JOB 状态并写入 TO 指定的变量。
 * 返回 MOTION_EXEC_OK 或 MOTION_EXEC_ERROR。
 */
static motion_exec_result_t execute_task(motion_vm_t *vm,
                                         const motion_instruction_t *ins)
{
    if (ins->command == MOTION_CMD_START_JOB) {
        int rc = motion_task_start(ins->name);

        if (rc != 0) {
            return exec_fail(vm, rc, "START JOB failed");
        }
        return MOTION_EXEC_OK;
    }

    if (ins->command == MOTION_CMD_STOP_JOB) {
        int rc = motion_task_stop(ins->name);

        if (rc != 0) {
            return exec_fail(vm, rc, "STOP JOB failed");
        }
        return MOTION_EXEC_OK;
    }

    if (ins->command == MOTION_CMD_JOB_STATE) {
        char target[MOTION_MAX_VAR_NAME];
        motion_state_t state;
        motion_value_t result;
        int has_target;

        if (motion_task_find(ins->name) < 0) {
            return param_fail(vm, "JOB not found");
        }
        state = motion_task_state(ins->name);

        has_target = copy_to_variable(ins, "TO", target, sizeof(target));
        if (has_target <= 0) {
            return param_fail(vm, "JOB STATE requires TO $VARIABLE");
        }

        memset(&result, 0, sizeof(result));
        result.type = MOTION_VALUE_INTEGER;
        result.data.integer = (int64_t)state;
        if (motion_variable_set(&vm->variables, target, &result) != 0) {
            return exec_fail(vm, MOTION_ERR_VARIABLE,
                             "too many variables");
        }
        return MOTION_EXEC_OK;
    }

    return param_fail(vm, "unsupported JOB action");
}

/* ------------------------------------------------------------------ */
/* 统一分发                                                            */
/* ------------------------------------------------------------------ */

/*
 * 统一指令分发入口。
 * 按 instruction->command 路由到对应 execute_*；SET 与 JOB/ON ERROR
 * 这类简单指令直接在此处理。
 * 返回见 motion_executor.h；vm 或 instruction 为 NULL 时返回
 * MOTION_EXEC_ERROR（不写错误信息）。
 */
motion_exec_result_t motion_executor_execute(
    motion_vm_t *vm, const motion_instruction_t *instruction)
{
    if (vm == NULL || instruction == NULL) {
        return MOTION_EXEC_ERROR;
    }

    switch (instruction->command) {
    case MOTION_CMD_STEP:
        return execute_step(vm, instruction);
    case MOTION_CMD_PWM:
        return execute_pwm(vm, instruction);
    case MOTION_CMD_GPIO:
        return execute_gpio(vm, instruction);
    case MOTION_CMD_RELAY:
        return execute_relay(vm, instruction);
    case MOTION_CMD_ADC:
        return execute_adc(vm, instruction);
    case MOTION_CMD_SENSOR:
        return execute_sensor(vm, instruction);
    case MOTION_CMD_WAIT:
        return execute_wait(vm, instruction);
    case MOTION_CMD_SET: {
        motion_value_t value;

        /* SET 先求值表达式，再写入同名变量 */
        if (eval_expression(vm, instruction->expression, &value) != 0) {
            return MOTION_EXEC_ERROR;
        }
        if (motion_variable_set(&vm->variables, instruction->name,
                                &value) != 0) {
            return exec_fail(vm, MOTION_ERR_VARIABLE,
                             "too many variables");
        }
        return MOTION_EXEC_OK;
    }
    case MOTION_CMD_IF:
    case MOTION_CMD_ELSE:
    case MOTION_CMD_END:
    case MOTION_CMD_LOOP:
    case MOTION_CMD_ENDLOOP:
    case MOTION_CMD_BREAK:
    case MOTION_CMD_LABEL:
    case MOTION_CMD_GOTO:
    case MOTION_CMD_CALL:
    case MOTION_CMD_RETURN:
        return execute_flow(vm, instruction);
    case MOTION_CMD_ERROR:
        return execute_error(vm, instruction);
    case MOTION_CMD_ON_ERROR: {
        const motion_error_entry_t *entry =
            motion_flow_find_error(instruction->offset);

        if (entry == NULL) {
            return exec_fail(vm, MOTION_ERR_FLOW,
                             "ON ERROR without ENDERROR");
        }
        /* 正常执行流不能进入异常处理块，直接跳到 ENDERROR 之后 */
        motion_vm_jump(vm, entry->enderror_next);
        return MOTION_EXEC_OK;
    }
    case MOTION_CMD_ENDERROR:
        return MOTION_EXEC_COMPLETE; /* 异常处理块结束，本段终止 */
    case MOTION_CMD_JOB: {
        const motion_job_entry_t *job =
            motion_flow_find_job_by_offset(instruction->offset);

        if (job == NULL) {
            /* 索引缺失时退化为顺序执行，避免卡死 */
            motion_vm_jump(vm, instruction->next_offset);
        } else {
            /* JOB 定义体不应被主流程执行，跳到 ENDJOB 之后 */
            motion_vm_jump(vm, job->next_offset);
        }
        return MOTION_EXEC_OK;
    }
    case MOTION_CMD_ENDJOB:
        return MOTION_EXEC_COMPLETE; /* JOB 体执行结束 */
    case MOTION_CMD_START_JOB:
    case MOTION_CMD_STOP_JOB:
    case MOTION_CMD_JOB_STATE:
        return execute_task(vm, instruction);
    default:
        return exec_fail(vm, MOTION_ERR_UNSUPPORTED,
                         "unsupported instruction");
    }
}
