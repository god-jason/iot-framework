#ifndef PORT_CAPTURE_H
#define PORT_CAPTURE_H

/*
 * 输入捕获：选择捕获边沿、预分频与滤波，读取捕获计数值。
 * 常用于测量脉宽、频率与占空比。
 *
 * 句柄由调用者静态分配，base 必须是首个成员。
 * 所有接口在 handle 为 NULL 时返回 PORT_INVALID_PARAM（查询类返回 0）。
 */

#include "port_def.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 捕获边沿 */
typedef enum {
    PORT_CAPTURE_RISING = 0,    /* 上升沿 */
    PORT_CAPTURE_FALLING,       /* 下降沿 */
    PORT_CAPTURE_BOTH           /* 双边沿 */
} port_capture_edge_t;

/* 捕获配置 */
typedef struct {
    port_capture_edge_t edge;   /* 捕获边沿 */
    uint32_t prescaler;         /* 捕获预分频，0 表示不分频 */
    uint32_t filter;            /* 数字滤波长度，0 表示不滤波 */
} port_capture_config_t;

/* 捕获句柄 */
typedef struct port_capture_handle {
    port_handle_base_t base;        /* 通用句柄基类，勿移动位置 */

    port_capture_config_t config;   /* 初始化时保存的配置副本 */

    port_callback_t callback;       /* 捕获事件回调，可为 NULL */
    void *user_data;                /* 回调上下文，可为 NULL */
} port_capture_handle_t;

/*
 * 初始化输入捕获。
 * handle : 句柄，不可为 NULL。
 * config : 配置，不可为 NULL。
 * 返回   : PORT_OK；PORT_INVALID_PARAM；引脚/通道不支持返回 PORT_NOT_SUPPORTED。
 */
port_status_t port_capture_init(port_capture_handle_t *handle,
                                const port_capture_config_t *config);

/* 启动捕获（轮询方式）；返回 PORT_OK / PORT_INVALID_PARAM / PORT_NOT_READY */
port_status_t port_capture_start(port_capture_handle_t *handle);

/* 停止捕获；返回 PORT_OK / PORT_INVALID_PARAM / PORT_NOT_READY */
port_status_t port_capture_stop(port_capture_handle_t *handle);

/* 启动捕获并使能中断，捕获事件通过 callback 上报 */
port_status_t port_capture_start_it(port_capture_handle_t *handle);

/*
 * 读取最近一次捕获的计数值。
 * handle : 句柄，可为 NULL。
 * 返回   : 捕获值；无捕获或参数非法时返回 0。
 */
uint32_t port_capture_get_value(port_capture_handle_t *handle);

#ifdef __cplusplus
}
#endif

#endif /* PORT_CAPTURE_H */
