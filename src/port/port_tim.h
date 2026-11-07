#ifndef PORT_TIM_H
#define PORT_TIM_H

/*
 * 通用定时器：预分频、周期与计数模式配置，启停、中断、运行期重载与计数值读写。
 * PWM/捕获/编码器为定时器的专用子功能，见各自头文件。
 *
 * 句柄由调用者静态分配，base 必须是首个成员。
 * 所有接口在 handle 为 NULL 时返回 PORT_INVALID_PARAM（查询类返回 0）。
 */

#include "port_def.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 计数模式 */
typedef enum {
    PORT_TIM_COUNTER_UP = 0,    /* 向上计数 */
    PORT_TIM_COUNTER_DOWN,      /* 向下计数 */
    PORT_TIM_COUNTER_CENTER     /* 中心对齐（上下交替） */
} port_tim_counter_t;

/* 定时器事件 */
typedef enum {
    PORT_TIM_EVENT_PERIOD = 0,  /* 周期溢出/更新 */
    PORT_TIM_EVENT_COMPARE,     /* 比较匹配 */
    PORT_TIM_EVENT_CAPTURE,     /* 输入捕获 */
    PORT_TIM_EVENT_UPDATE       /* 更新事件 */
} port_tim_event_t;

/* 定时器配置 */
typedef struct {
    uint32_t prescaler;             /* 预分频系数，计数频率 = 定时器时钟 / (prescaler + 1) */
    uint32_t period;                /* 自动重载值，决定计数周期 */

    port_tim_counter_t counter_mode;/* 计数模式 */

    uint8_t clock_division;         /* 时钟分频档位编号（平台定义），0 表示不分频 */
} port_tim_config_t;

/* 定时器句柄 */
typedef struct port_tim_handle {
    port_handle_base_t base;        /* 通用句柄基类，勿移动位置 */

    port_tim_config_t config;       /* 初始化时保存的配置副本 */

    port_callback_t callback;       /* 周期/比较/捕获事件回调，可为 NULL */
    void *user_data;                /* 回调上下文，可为 NULL */
} port_tim_handle_t;

/*
 * 初始化定时器（不启动计数）。
 * handle : 句柄，不可为 NULL。
 * config : 配置，不可为 NULL；period 需大于 0。
 * 返回   : PORT_OK；PORT_INVALID_PARAM；计数模式不支持返回 PORT_NOT_SUPPORTED。
 */
port_status_t port_tim_init(port_tim_handle_t *handle, const port_tim_config_t *config);

/* 反初始化定时器；返回 PORT_OK / PORT_INVALID_PARAM / PORT_NOT_READY */
port_status_t port_tim_deinit(port_tim_handle_t *handle);

/* 启动计数（轮询方式）；返回 PORT_OK / PORT_INVALID_PARAM / PORT_NOT_READY */
port_status_t port_tim_start(port_tim_handle_t *handle);

/* 停止计数；返回 PORT_OK / PORT_INVALID_PARAM / PORT_NOT_READY */
port_status_t port_tim_stop(port_tim_handle_t *handle);

/* 启动计数并使能更新中断（事件通过 callback 上报） */
port_status_t port_tim_start_it(port_tim_handle_t *handle);

/* 停止计数并关闭更新中断 */
port_status_t port_tim_stop_it(port_tim_handle_t *handle);

/*
 * 运行期修改自动重载值。
 * period : 新的周期值；0 在部分平台会被硬件解释为最大值，需注意。
 * 返回   : PORT_OK；PORT_INVALID_PARAM；未初始化返回 PORT_NOT_READY。
 */
port_status_t port_tim_set_period(port_tim_handle_t *handle, uint32_t period);

/*
 * 运行期修改预分频。
 * prescaler : 新的预分频系数，实际分频为 prescaler + 1。
 * 返回      : PORT_OK；PORT_INVALID_PARAM；未初始化返回 PORT_NOT_READY。
 * 注意      : 新分频通常在下一个更新事件生效。
 */
port_status_t port_tim_set_prescaler(port_tim_handle_t *handle, uint32_t prescaler);

/*
 * 直接写入计数器当前值。
 * counter : 目标计数值，超出 period 时行为由硬件决定。
 * 返回    : PORT_OK；PORT_INVALID_PARAM；未初始化返回 PORT_NOT_READY。
 */
port_status_t port_tim_set_counter(port_tim_handle_t *handle, uint32_t counter);

/*
 * 读取计数器当前值。
 * handle : 句柄，可为 NULL。
 * 返回   : 计数值；handle 为 NULL 或未初始化时返回 0。
 */
uint32_t port_tim_get_counter(port_tim_handle_t *handle);

#ifdef __cplusplus
}
#endif

#endif /* PORT_TIM_H */
