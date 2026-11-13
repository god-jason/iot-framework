#ifndef DEVICE_H
#define DEVICE_H

/*
 * device.h - 通用设备命令组件（GPIO / PWM）
 *
 * device_init() 把 gpio / pwm 命令注册到 cmd 全局命令表：
 *   gpio <mode|set|get|toggle|deinit> <pin> [arg1] [arg2]
 *   pwm  list | <name|index> <start|stop|freq|duty|info> [value]
 *
 * PWM 通道由平台初始化后通过 device_pwm_bind() 绑定名字。
 */

#include "hal.h"
#include "ros.h"
#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DEVICE_PWM_MAX       8U
#define DEVICE_PWM_NAME_MAX  16U

/* 注册 gpio / pwm 命令（幂等）；返回 0 成功，-1 失败 */
int device_init(void);

/* 绑定 / 解绑 PWM 通道（按名字） */
int device_pwm_bind(const char *name, hal_pwm_t *handle);
int device_pwm_unbind(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* DEVICE_H */
