#ifndef PORT_USB_H
#define PORT_USB_H

/*
 * USB 主机/设备控制器：模式与速率配置、启停及 D+/D- 连接控制。
 *
 * 句柄由调用者静态分配，base 必须是首个成员。
 * 所有接口在 handle 为 NULL 时返回 PORT_INVALID_PARAM。
 * 枚举与类驱动由上层协议栈完成，本模块只负责控制器生命周期。
 */

#include "port_def.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 工作模式 */
typedef enum {
    PORT_USB_MODE_DEVICE = 0,   /* 设备模式 */
    PORT_USB_MODE_HOST          /* 主机模式 */
} port_usb_mode_t;

/* 总线速率 */
typedef enum {
    PORT_USB_SPEED_FULL = 0,    /* 全速 12 Mbps */
    PORT_USB_SPEED_HIGH,        /* 高速 480 Mbps */
    PORT_USB_SPEED_LOW          /* 低速 1.5 Mbps（仅主机） */
} port_usb_speed_t;

/* USB 配置 */
typedef struct {
    port_usb_mode_t mode;       /* 工作模式 */
    port_usb_speed_t speed;     /* 总线速率 */
} port_usb_config_t;

/* USB 句柄 */
typedef struct port_usb_handle {
    port_handle_base_t base;    /* 通用句柄基类，勿移动位置 */
    port_usb_config_t config;   /* 初始化时保存的配置副本 */
    port_callback_t callback;   /* 连接/断开/错误回调，可为 NULL */
    void *user_data;            /* 回调上下文，可为 NULL */
} port_usb_handle_t;

/*
 * 初始化 USB 控制器。
 * handle : 句柄，不可为 NULL。
 * config : 配置，不可为 NULL；模式与速率组合需硬件支持。
 * 返回   : PORT_OK；PORT_INVALID_PARAM；组合不支持返回 PORT_NOT_SUPPORTED。
 */
port_status_t port_usb_init(port_usb_handle_t *handle, const port_usb_config_t *config);

/* 反初始化 USB 控制器；返回 PORT_OK / PORT_INVALID_PARAM / PORT_NOT_READY */
port_status_t port_usb_deinit(port_usb_handle_t *handle);

/* 启动控制器（开始处理总线事件）；返回 PORT_OK / PORT_INVALID_PARAM / PORT_NOT_READY */
port_status_t port_usb_start(port_usb_handle_t *handle);

/* 停止控制器；返回 PORT_OK / PORT_INVALID_PARAM / PORT_NOT_READY */
port_status_t port_usb_stop(port_usb_handle_t *handle);

/* 设备模式下连接 D+ 上拉（主机可见）；返回 PORT_OK / PORT_INVALID_PARAM / PORT_NOT_READY */
port_status_t port_usb_connect(port_usb_handle_t *handle);

/* 设备模式下断开 D+ 上拉；返回 PORT_OK / PORT_INVALID_PARAM / PORT_NOT_READY */
port_status_t port_usb_disconnect(port_usb_handle_t *handle);

#ifdef __cplusplus
}
#endif

#endif /* PORT_USB_H */
