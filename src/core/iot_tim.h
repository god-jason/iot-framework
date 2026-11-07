#ifndef IOT_TIM_H
#define IOT_TIM_H

/*
 * 通用定时器：预分频、周期与计数模式配置，启停、中断、运行期重载与计数值读写。
 * PWM/捕获/编码器为定时器的专用子功能，见各自头文件。
 *
 * 句柄由调用者静态分配，base 必须是首个成员。
 * 所有接口在 handle 为 NULL 时返回 IOT_INVALID_PARAM（查询类返回 0）。
 */

#include "iot_def.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 计数模式 */
typedef enum {
    IOT_TIM_COUNTER_UP = 0,    /* 向上计数 */
    IOT_TIM_COUNTER_DOWN,      /* 向下计数 */
    IOT_TIM_COUNTER_CENTER     /* 中心对齐（上下交替） */
} iot_tim_counter_t;

/* 定时器事件 */
typedef enum {
    IOT_TIM_EVENT_PERIOD = 0,  /* 周期溢出/更新 */
    IOT_TIM_EVENT_COMPARE,     /* 比较匹配 */
    IOT_TIM_EVENT_CAPTURE,     /* 输入捕获 */
    IOT_TIM_EVENT_UPDATE       /* 更新事件 */
} iot_tim_event_t;

/* 定时器配置 */
typedef struct {
    uint32_t prescaler;             /* 预分频系数，计数频率 = 定时器时钟 / (prescaler + 1) */
    uint32_t period;                /* 自动重载值，决定计数周期 */

    iot_tim_counter_t counter_mode;/* 计数模式 */

    uint8_t clock_division;         /* 时钟分频档位编号（平台定义），0 表示不分频 */
} iot_tim_config_t;

/* 定时器句柄 */
typedef struct iot_tim {
    iot_handle_base_t base;        /* 通用句柄基类，勿移动位置 */

    iot_tim_config_t config;       /* 初始化时保存的配置副本 */

    iot_callback_t callback;       /* 周期/比较/捕获事件回调，可为 NULL */
    void *user_data;                /* 回调上下文，可为 NULL */
} iot_tim_t;

/*
 * 初始化定时器（不启动计数）。
 * handle : 句柄，不可为 NULL。
 * config : 配置，不可为 NULL；period 需大于 0。
 * 返回   : IOT_OK；IOT_INVALID_PARAM；计数模式不支持返回 IOT_NOT_SUPPORTED。
 */
iot_status_t iot_tim_init(iot_tim_t *handle, const iot_tim_config_t *config);

/* 反初始化定时器；返回 IOT_OK / IOT_INVALID_PARAM / IOT_NOT_READY */
iot_status_t iot_tim_deinit(iot_tim_t *handle);

/* 启动计数（轮询方式）；返回 IOT_OK / IOT_INVALID_PARAM / IOT_NOT_READY */
iot_status_t iot_tim_start(iot_tim_t *handle);

/* 停止计数；返回 IOT_OK / IOT_INVALID_PARAM / IOT_NOT_READY */
iot_status_t iot_tim_stop(iot_tim_t *handle);

/* 启动计数并使能更新中断（事件通过 callback 上报） */
iot_status_t iot_tim_start_it(iot_tim_t *handle);

/* 停止计数并关闭更新中断 */
iot_status_t iot_tim_stop_it(iot_tim_t *handle);

/*
 * 运行期修改自动重载值。
 * period : 新的周期值；0 在部分平台会被硬件解释为最大值，需注意。
 * 返回   : IOT_OK；IOT_INVALID_PARAM；未初始化返回 IOT_NOT_READY。
 */
iot_status_t iot_tim_set_period(iot_tim_t *handle, uint32_t period);

/*
 * 运行期修改预分频。
 * prescaler : 新的预分频系数，实际分频为 prescaler + 1。
 * 返回      : IOT_OK；IOT_INVALID_PARAM；未初始化返回 IOT_NOT_READY。
 * 注意      : 新分频通常在下一个更新事件生效。
 */
iot_status_t iot_tim_set_prescaler(iot_tim_t *handle, uint32_t prescaler);

/*
 * 直接写入计数器当前值。
 * counter : 目标计数值，超出 period 时行为由硬件决定。
 * 返回    : IOT_OK；IOT_INVALID_PARAM；未初始化返回 IOT_NOT_READY。
 */
iot_status_t iot_tim_set_counter(iot_tim_t *handle, uint32_t counter);

/*
 * 读取计数器当前值。
 * handle : 句柄，可为 NULL。
 * 返回   : 计数值；handle 为 NULL 或未初始化时返回 0。
 */
uint32_t iot_tim_get_counter(iot_tim_t *handle);

/* ---------------- 正交编码器 ---------------- */
/*
 * 正交编码器接口：按 TI1/TI2/TI12 模式解码 A/B 相，读取与设置计数值。
 * 底层复用定时器编码器模式。
 */

/* 编码器计数模式 */
typedef enum {
    IOT_ENCODER_MODE_TI1 = 0,  /* 仅 TI1 计数（单相） */
    IOT_ENCODER_MODE_TI2,      /* 仅 TI2 计数（单相） */
    IOT_ENCODER_MODE_TI12      /* TI1 与 TI2 正交解码（四倍频） */
} iot_encoder_mode_t;

/* 编码器配置 */
typedef struct {
    iot_encoder_mode_t mode;   /* 计数模式 */

    uint32_t period;            /* 计数上限（自动重载值），到达后回绕 */
    uint32_t filter;            /* 输入滤波长度，0 表示不滤波 */

    bool reverse;               /* true 表示反转计数方向 */
} iot_encoder_config_t;

/* 编码器句柄 */
typedef struct iot_encoder {
    iot_handle_base_t base;        /* 通用句柄基类，勿移动位置 */
    iot_encoder_config_t config;   /* 初始化时保存的配置副本 */
} iot_encoder_t;

/*
 * 初始化编码器接口。
 * handle : 句柄，不可为 NULL。
 * config : 配置，不可为 NULL；period 需大于 0。
 * 返回   : IOT_OK；IOT_INVALID_PARAM；模式不支持返回 IOT_NOT_SUPPORTED。
 */
iot_status_t iot_encoder_init(iot_encoder_t *handle,
                                const iot_encoder_config_t *config);

/* 启动计数；返回 IOT_OK / IOT_INVALID_PARAM / IOT_NOT_READY */
iot_status_t iot_encoder_start(iot_encoder_t *handle);

/* 停止计数并保持当前值；返回 IOT_OK / IOT_INVALID_PARAM / IOT_NOT_READY */
iot_status_t iot_encoder_stop(iot_encoder_t *handle);

/*
 * 读取当前计数值（带符号，已按 reverse 处理方向）。
 * handle : 句柄，可为 NULL。
 * 返回   : 计数值；handle 为 NULL 或未初始化时返回 0。
 */
int32_t iot_encoder_get_count(iot_encoder_t *handle);

/*
 * 设置计数值，常用于清零或设定参考位置。
 * count : 目标计数值（带符号）。
 * 返回  : IOT_OK；IOT_INVALID_PARAM；未初始化返回 IOT_NOT_READY。
 */
iot_status_t iot_encoder_set_count(iot_encoder_t *handle, int32_t count);

#ifdef __cplusplus
}
#endif

#endif /* IOT_TIM_H */
