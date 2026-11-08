#ifndef HAL_USB_H
#define HAL_USB_H

/*
 * USB 主机/设备控制器：模式与速率配置、启停及 D+/D- 连接控制。
 *
 * 句柄由调用者静态分配，base 必须是首个成员。
 * 所有接口在 handle 为 NULL 时返回 HAL_INVALID_PARAM。
 * 枚举与类驱动由上层协议栈完成，本模块只负责控制器生命周期。
 */

#include "hal_def.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 工作模式 */
typedef enum {
    HAL_USB_MODE_DEVICE = 0,   /* 设备模式 */
    HAL_USB_MODE_HOST          /* 主机模式 */
} hal_usb_mode_t;

/* 总线速率 */
typedef enum {
    HAL_USB_SPEED_FULL = 0,    /* 全速 12 Mbps */
    HAL_USB_SPEED_HIGH,        /* 高速 480 Mbps */
    HAL_USB_SPEED_LOW          /* 低速 1.5 Mbps（仅主机） */
} hal_usb_speed_t;

/* USB 配置 */
typedef struct {
    hal_usb_mode_t mode;       /* 工作模式 */
    hal_usb_speed_t speed;     /* 总线速率 */
} hal_usb_config_t;

/* USB 句柄 */
typedef struct hal_usb {
    hal_handle_base_t base;    /* 通用句柄基类，勿移动位置 */
    hal_usb_config_t config;   /* 初始化时保存的配置副本 */
    hal_callback_t callback;   /* 连接/断开/错误回调，可为 NULL */
    void *user_data;            /* 回调上下文，可为 NULL */
} hal_usb_t;

/*
 * 初始化 USB 控制器。
 * handle : 句柄，不可为 NULL。
 * config : 配置，不可为 NULL；模式与速率组合需硬件支持。
 * 返回   : HAL_OK；HAL_INVALID_PARAM；组合不支持返回 HAL_NOT_SUPPORTED。
 */
hal_status_t hal_usb_init(hal_usb_t *handle, const hal_usb_config_t *config);

/* 反初始化 USB 控制器；返回 HAL_OK / HAL_INVALID_PARAM / HAL_NOT_READY */
hal_status_t hal_usb_deinit(hal_usb_t *handle);

/* 启动控制器（开始处理总线事件）；返回 HAL_OK / HAL_INVALID_PARAM / HAL_NOT_READY */
hal_status_t hal_usb_start(hal_usb_t *handle);

/* 停止控制器；返回 HAL_OK / HAL_INVALID_PARAM / HAL_NOT_READY */
hal_status_t hal_usb_stop(hal_usb_t *handle);

/* 设备模式下连接 D+ 上拉（主机可见）；返回 HAL_OK / HAL_INVALID_PARAM / HAL_NOT_READY */
hal_status_t hal_usb_connect(hal_usb_t *handle);

/* 设备模式下断开 D+ 上拉；返回 HAL_OK / HAL_INVALID_PARAM / HAL_NOT_READY */
hal_status_t hal_usb_disconnect(hal_usb_t *handle);

#ifdef __cplusplus
}
#endif

#endif /* HAL_USB_H */
