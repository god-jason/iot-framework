#ifndef IOT_CAPTURE_H
#define IOT_CAPTURE_H

/*
 * 输入捕获：选择捕获边沿、预分频与滤波，读取捕获计数值。
 * 常用于测量脉宽、频率与占空比。
 *
 * 句柄由调用者静态分配，base 必须是首个成员。
 * 所有接口在 handle 为 NULL 时返回 IOT_INVALID_PARAM（查询类返回 0）。
 */

#include "iot_def.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 捕获边沿 */
typedef enum {
    IOT_CAPTURE_RISING = 0,    /* 上升沿 */
    IOT_CAPTURE_FALLING,       /* 下降沿 */
    IOT_CAPTURE_BOTH           /* 双边沿 */
} iot_capture_edge_t;

/* 捕获配置 */
typedef struct {
    iot_capture_edge_t edge;   /* 捕获边沿 */
    uint32_t prescaler;         /* 捕获预分频，0 表示不分频 */
    uint32_t filter;            /* 数字滤波长度，0 表示不滤波 */
} iot_capture_config_t;

/* 捕获句柄 */
typedef struct iot_capture {
    iot_handle_base_t base;        /* 通用句柄基类，勿移动位置 */

    iot_capture_config_t config;   /* 初始化时保存的配置副本 */

    iot_callback_t callback;       /* 捕获事件回调，可为 NULL */
    void *user_data;                /* 回调上下文，可为 NULL */
} iot_capture_t;

/*
 * 初始化输入捕获。
 * handle : 句柄，不可为 NULL。
 * config : 配置，不可为 NULL。
 * 返回   : IOT_OK；IOT_INVALID_PARAM；引脚/通道不支持返回 IOT_NOT_SUPPORTED。
 */
iot_status_t iot_capture_init(iot_capture_t *handle,
                                const iot_capture_config_t *config);

/* 启动捕获（轮询方式）；返回 IOT_OK / IOT_INVALID_PARAM / IOT_NOT_READY */
iot_status_t iot_capture_start(iot_capture_t *handle);

/* 停止捕获；返回 IOT_OK / IOT_INVALID_PARAM / IOT_NOT_READY */
iot_status_t iot_capture_stop(iot_capture_t *handle);

/* 启动捕获并使能中断，捕获事件通过 callback 上报 */
iot_status_t iot_capture_start_it(iot_capture_t *handle);

/*
 * 读取最近一次捕获的计数值。
 * handle : 句柄，可为 NULL。
 * 返回   : 捕获值；无捕获或参数非法时返回 0。
 */
uint32_t iot_capture_get_value(iot_capture_t *handle);

#ifdef __cplusplus
}
#endif

#endif /* IOT_CAPTURE_H */
