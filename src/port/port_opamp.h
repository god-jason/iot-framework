#ifndef PORT_OPAMP_H
#define PORT_OPAMP_H

/*
 * 片内运算放大器：跟随器或 PGA 模式、增益与输入选择，启停控制。
 * 常用于模拟前端信号调理。
 *
 * 句柄由调用者静态分配，base 必须是首个成员。
 * 所有接口在 handle 为 NULL 时返回 PORT_INVALID_PARAM。
 */

#include "port_def.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 工作模式 */
typedef enum {
    PORT_OPAMP_MODE_FOLLOWER = 0,   /* 电压跟随器，增益为 1 */
    PORT_OPAMP_MODE_PGA             /* 可编程增益放大 */
} port_opamp_mode_t;

/* 运放配置 */
typedef struct {
    port_opamp_mode_t mode;     /* 工作模式 */
    uint8_t gain;               /* PGA 增益档位编号（平台定义），跟随器模式忽略 */
    uint8_t positive_input;     /* 正端输入源编号 */
    uint8_t negative_input;     /* 负端输入源编号，PGA 模式下通常由内部反馈网络决定 */
} port_opamp_config_t;

/* 运放句柄 */
typedef struct port_opamp_handle {
    port_handle_base_t base;        /* 通用句柄基类，勿移动位置 */
    port_opamp_config_t config;     /* 初始化时保存的配置副本 */
} port_opamp_handle_t;

/*
 * 初始化运放。
 * handle : 句柄，不可为 NULL。
 * config : 配置，不可为 NULL；增益档位需平台支持。
 * 返回   : PORT_OK；PORT_INVALID_PARAM；增益/输入不支持返回 PORT_NOT_SUPPORTED。
 */
port_status_t port_opamp_init(port_opamp_handle_t *handle, const port_opamp_config_t *config);

/* 使能运放输出；返回 PORT_OK / PORT_INVALID_PARAM / PORT_NOT_READY */
port_status_t port_opamp_start(port_opamp_handle_t *handle);

/* 关闭运放以降低功耗；返回 PORT_OK / PORT_INVALID_PARAM / PORT_NOT_READY */
port_status_t port_opamp_stop(port_opamp_handle_t *handle);

#ifdef __cplusplus
}
#endif

#endif /* PORT_OPAMP_H */
