#ifndef ROS_EVENT_H
#define ROS_EVENT_H

/*
 * ros_event.h - 事件组（ros_event.c）
 *
 * 32 位事件标志，支持「任一/全部满足」等待，以及返回前自动清位。
 * 典型用途：用一组位同时等待「网络就绪 + 配置完成 + 允许上报」。
 */

#include "ros_types.h"
#include "ros_wait.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 等待选项 */
#define ROS_EVENT_ANY    0x00U   /* 任一位满足即返回 */
#define ROS_EVENT_ALL    0x01U   /* 所有请求位满足才返回 */
#define ROS_EVENT_CLEAR  0x02U   /* 返回前清除已满足的位 */

struct ros_event {
    uint32_t flags;                 /* 当前事件标志位 */
    ros_wait_queue_t wait;         /* 等待该事件的任务 */
    bool initialized;               /* 初始化标志 */
};

#if ROS_USE_EVENT

/* 初始化事件组，标志位清零 */
int ros_event_init(ros_event_t *event);

/* 动态创建/销毁：对象由 ros_mem_* 分配，失败返回 NULL */
ros_event_t *ros_event_create(void);

/* 销毁动态事件组；仍有任务等待时返回 ROS_BUSY */
int ros_event_destroy(ros_event_t *event);

/* 置位，并唤醒所有因此满足等待条件的任务 */
int ros_event_set(ros_event_t *event, uint32_t bits);

/* 清位 */
int ros_event_clear(ros_event_t *event, uint32_t bits);

/* 读取当前标志位 */
int ros_event_get(ros_event_t *event, uint32_t *flags);

/*
 * 等待事件位。
 *   bits    : 关注的位（不能为 0）
 *   options : ROS_EVENT_ANY / ROS_EVENT_ALL / ROS_EVENT_CLEAR 组合
 *   timeout : ROS_TIMEOUT_FOREVER 永久等待；ROS_TIMEOUT_NONE 立即返回
 *   out_flags: 可空，返回满足时刻的完整标志位
 * 返回 ROS_OK / ROS_TIMEOUT / ROS_INVALID_PARAM / ROS_NOT_READY
 */
int ros_event_wait(ros_event_t *event, uint32_t bits, uint8_t options,
                   uint32_t timeout, uint32_t *out_flags);

#endif /* ROS_USE_EVENT */

#ifdef __cplusplus
}
#endif

#endif /* ROS_EVENT_H */
