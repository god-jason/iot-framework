#ifndef SAFETY_H
#define SAFETY_H

/*
 * safety.h - 机器人安全状态管理
 *
 * 统一管理急停、限位、过流/过温、通信异常等故障：
 *   - 瞬时故障位 faults：条件消失后可自动恢复；
 *   - 锁定故障位 latched：必须显式 clear 才能恢复（如过流、硬件异常）。
 * 运动许可判断只依赖 faults/latched/estop 三者。
 *
 * 无动态内存，不直接访问硬件；限位/电流等由调用方读取后上报。
 */

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 故障位定义 */
#define SAFETY_FAULT_ESTOP         (1UL << 0)   /* 急停按下 */
#define SAFETY_FAULT_LIMIT_MIN     (1UL << 1)   /* 负向限位触发 */
#define SAFETY_FAULT_LIMIT_MAX     (1UL << 2)   /* 正向限位触发 */
#define SAFETY_FAULT_OVERCURRENT   (1UL << 3)   /* 过流 */
#define SAFETY_FAULT_OVERTEMP      (1UL << 4)   /* 过温 */
#define SAFETY_FAULT_COMM          (1UL << 5)   /* 通信中断 */
#define SAFETY_FAULT_WATCHDOG      (1UL << 6)   /* 看门狗/任务超时 */
#define SAFETY_FAULT_USER          (1UL << 7)   /* 用户自定义 */

typedef struct {
    uint32_t faults;        /* 当前故障位（含锁定） */
    uint32_t latched;       /* 锁定故障位 */
    bool estop;             /* 急停状态 */
    uint32_t estop_count;   /* 急停触发次数（诊断用） */
    uint32_t fault_count;   /* 故障触发次数（诊断用） */
} safety_t;

/* 初始化为无故障、未急停 */
int safety_init(safety_t *safety);

/*
 * 设置急停状态；active=true 时置位并计数。
 * 返回：0 成功；-1 参数非法。
 */
int safety_estop(safety_t *safety, bool active);

/*
 * 上报一个故障位（可多位或运算）；latched=true 表示锁定。
 * 返回：0 成功；-1 参数非法或 code 为 0。
 */
int safety_raise(safety_t *safety, uint32_t code, bool latched);

/*
 * 清除故障位（锁定故障也一并清除）。
 * 急停位需通过 safety_estop(false) 清除。
 * 返回：0 成功；-1 参数非法。
 */
int safety_clear(safety_t *safety, uint32_t code);

/* 当前故障位（含急停位与锁定位） */
uint32_t safety_faults(const safety_t *safety);

/* 是否允许运动：无故障且未急停 */
bool safety_motion_allowed(const safety_t *safety);

/*
 * 限位检查：min_limit/max_limit 为硬件限位状态。
 * 同时触发或都不触发时不做处理；单侧触发则上报对应故障位。
 * 返回：更新后的故障位；参数非法返回 0。
 */
uint32_t safety_check_limits(safety_t *safety, bool min_limit, bool max_limit);

#ifdef __cplusplus
}
#endif

#endif /* SAFETY_H */
