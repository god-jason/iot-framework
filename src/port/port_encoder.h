#ifndef PORT_ENCODER_H
#define PORT_ENCODER_H

/*
 * 正交编码器接口：按 TI1/TI2/TI12 模式解码 A/B 相，读取与设置计数值。
 * 底层复用定时器编码器模式。
 *
 * 句柄由调用者静态分配，base 必须是首个成员。
 * 所有接口在 handle 为 NULL 时返回 PORT_INVALID_PARAM（查询类返回 0）。
 */

#include "port_def.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 编码器计数模式 */
typedef enum {
    PORT_ENCODER_MODE_TI1 = 0,  /* 仅 TI1 计数（单相） */
    PORT_ENCODER_MODE_TI2,      /* 仅 TI2 计数（单相） */
    PORT_ENCODER_MODE_TI12      /* TI1 与 TI2 正交解码（四倍频） */
} port_encoder_mode_t;

/* 编码器配置 */
typedef struct {
    port_encoder_mode_t mode;   /* 计数模式 */

    uint32_t period;            /* 计数上限（自动重载值），到达后回绕 */
    uint32_t filter;            /* 输入滤波长度，0 表示不滤波 */

    bool reverse;               /* true 表示反转计数方向 */
} port_encoder_config_t;

/* 编码器句柄 */
typedef struct port_encoder_handle {
    port_handle_base_t base;        /* 通用句柄基类，勿移动位置 */
    port_encoder_config_t config;   /* 初始化时保存的配置副本 */
} port_encoder_handle_t;

/*
 * 初始化编码器接口。
 * handle : 句柄，不可为 NULL。
 * config : 配置，不可为 NULL；period 需大于 0。
 * 返回   : PORT_OK；PORT_INVALID_PARAM；模式不支持返回 PORT_NOT_SUPPORTED。
 */
port_status_t port_encoder_init(port_encoder_handle_t *handle,
                                const port_encoder_config_t *config);

/* 启动计数；返回 PORT_OK / PORT_INVALID_PARAM / PORT_NOT_READY */
port_status_t port_encoder_start(port_encoder_handle_t *handle);

/* 停止计数并保持当前值；返回 PORT_OK / PORT_INVALID_PARAM / PORT_NOT_READY */
port_status_t port_encoder_stop(port_encoder_handle_t *handle);

/*
 * 读取当前计数值（带符号，已按 reverse 处理方向）。
 * handle : 句柄，可为 NULL。
 * 返回   : 计数值；handle 为 NULL 或未初始化时返回 0。
 */
int32_t port_encoder_get_count(port_encoder_handle_t *handle);

/*
 * 设置计数值，常用于清零或设定参考位置。
 * count : 目标计数值（带符号）。
 * 返回  : PORT_OK；PORT_INVALID_PARAM；未初始化返回 PORT_NOT_READY。
 */
port_status_t port_encoder_set_count(port_encoder_handle_t *handle, int32_t count);

#ifdef __cplusplus
}
#endif

#endif /* PORT_ENCODER_H */
