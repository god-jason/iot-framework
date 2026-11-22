#ifndef ROS_RTC_H
#define ROS_RTC_H

/*
 * ros_rtc.h - 实时时钟抽象（Unix 时间）
 *
 * 本模块只定义时间后端接口与转发逻辑，不包含任何具体实现：
 *   - boards 层负责实现并注册后端（硬件 RTC 寄存器 / DS1302 等时钟芯片，
 *     或基于内核 tick 分频的 1Hz 软件计时）；
 *   - 未注册后端时，读取返回默认时间，写入返回 ROS_NOT_READY。
 *
 * 默认时间：2026-11-22 00:00:00 UTC，Unix 秒 = 1795305600。
 * 时间统一按 UTC 秒处理，显示本地时间由上层换算。
 */

#include "ros_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 默认时间 2026-11-22 00:00:00 UTC 对应的 Unix 秒 */
#define ROS_RTC_DEFAULT_UNIX   1795305600UL

/* 时间源类型（后端注册时声明，供显示/诊断使用） */
typedef enum {
    ROS_RTC_SOURCE_NONE = 0,    /* 未注册后端 */
    ROS_RTC_SOURCE_HW,          /* 硬件 RTC / 时钟芯片 */
    ROS_RTC_SOURCE_TICK         /* 基于内核 tick 的软件计时 */
} ros_rtc_source_e;

/*
 * 时间后端操作表（由 boards 层提供静态实例）。
 *   get : 读取当前 Unix 秒，成功返回 ROS_OK 并写入 *timestamp；
 *   set : 设置 Unix 秒，成功返回 ROS_OK；
 *   name: 后端名称（可为 NULL，仅用于诊断）；
 *   source: 时间源类型。
 */
typedef struct {
    const char *name;
    ros_rtc_source_e source;
    int (*get)(uint32_t *timestamp);
    int (*set)(uint32_t timestamp);
} ros_rtc_ops_t;

/*
 * 注册时间后端（覆盖之前注册的后端）。
 * 返回 ROS_OK；ops 为 NULL 或 get/set 缺失返回 ROS_INVALID_PARAM。
 */
int ros_rtc_register(const ros_rtc_ops_t *ops);

/* 注销当前后端，回到未注册状态。 */
void ros_rtc_unregister(void);

/*
 * 当前 Unix 秒（自 1970-01-01 00:00:00 UTC）。
 * 未注册后端或后端读取失败时返回 ROS_RTC_DEFAULT_UNIX。
 */
uint32_t ros_rtc_get_timestamp(void);

/*
 * 设置 Unix 秒（转发给后端）。
 * 返回后端结果；未注册后端返回 ROS_NOT_READY。
 */
int ros_rtc_set_timestamp(uint32_t timestamp);

/* 当前时间源；未注册返回 ROS_RTC_SOURCE_NONE。 */
ros_rtc_source_e ros_rtc_source(void);

/* 当前后端名称；未注册返回 NULL。 */
const char *ros_rtc_backend_name(void);

#ifdef __cplusplus
}
#endif

#endif /* ROS_RTC_H */
