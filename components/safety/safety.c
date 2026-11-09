/*
 * safety.c - 安全状态管理实现
 *
 * faults 是当前故障集合；latched 记录必须人工清除的故障。
 * 急停同时体现在 estop 与 SAFETY_FAULT_ESTOP 位，便于统一判断。
 */

#include "safety.h"

#include <stddef.h>

int safety_init(safety_t *safety)
{
    if (safety == NULL) {
        return -1;
    }
    safety->faults = 0U;
    safety->latched = 0U;
    safety->estop = false;
    safety->estop_count = 0U;
    safety->fault_count = 0U;
    return 0;
}

int safety_estop(safety_t *safety, bool active)
{
    if (safety == NULL) {
        return -1;
    }
    if (active) {
        if (!safety->estop) {
            safety->estop_count++;
        }
        safety->estop = true;
        safety->faults |= SAFETY_FAULT_ESTOP;
    } else {
        safety->estop = false;
        safety->faults &= ~SAFETY_FAULT_ESTOP;
    }
    return 0;
}

int safety_raise(safety_t *safety, uint32_t code, bool latched)
{
    if (safety == NULL || code == 0U) {
        return -1;
    }
    safety->faults |= code;
    if (latched) {
        safety->latched |= code;
    }
    safety->fault_count++;
    return 0;
}

int safety_clear(safety_t *safety, uint32_t code)
{
    if (safety == NULL) {
        return -1;
    }
    safety->faults &= ~code;
    safety->latched &= ~code;
    if ((code & SAFETY_FAULT_ESTOP) != 0U) {
        safety->estop = false;
    }
    return 0;
}

uint32_t safety_faults(const safety_t *safety)
{
    return (safety != NULL) ? safety->faults : 0U;
}

bool safety_motion_allowed(const safety_t *safety)
{
    if (safety == NULL) {
        return false;
    }
    return (!safety->estop) && (safety->faults == 0U);
}

uint32_t safety_check_limits(safety_t *safety, bool min_limit, bool max_limit)
{
    if (safety == NULL) {
        return 0U;
    }

    if (min_limit && max_limit) {
        /* 两侧同时触发通常是接线/逻辑错误，按用户故障处理 */
        (void)safety_raise(safety, SAFETY_FAULT_USER, false);
    } else if (min_limit) {
        (void)safety_raise(safety, SAFETY_FAULT_LIMIT_MIN, false);
    } else if (max_limit) {
        (void)safety_raise(safety, SAFETY_FAULT_LIMIT_MAX, false);
    } else {
        /* 限位全部释放：限位故障自动恢复 */
        safety->faults &= ~(SAFETY_FAULT_LIMIT_MIN | SAFETY_FAULT_LIMIT_MAX);
        safety->latched &= ~(SAFETY_FAULT_LIMIT_MIN | SAFETY_FAULT_LIMIT_MAX);
    }
    return safety->faults;
}
