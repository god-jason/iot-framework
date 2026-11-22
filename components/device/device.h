#ifndef DEVICE_H
#define DEVICE_H

/*
 * device.h - 通用设备命令组件（GPIO / PWM）
 *
 * device_init() 把 gpio / pwm 命令注册到 cmd 全局命令表：
 *   gpio <mode|set|get|toggle|deinit> <pin> [arg1] [arg2]
 *   pwm  list | <index> <start|stop|freq|duty|info> [value]
 *
 * PWM 通道按注册顺序编号 0..N-1，命令只使用编号，不使用名字。
 */

#include "hal.h"
#include "ros.h"
#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DEVICE_PWM_MAX       12U

/* 注册 gpio / pwm 命令（幂等）；返回 0 成功，-1 失败 */
int device_init(void);

/* 绑定 PWM 通道：按调用顺序分配编号 0..DEVICE_PWM_MAX-1 */
int device_pwm_bind(hal_pwm_t *handle);

/* 解绑指定编号的 PWM 通道 */
int device_pwm_unbind(uint32_t index);

#ifdef __cplusplus
}
#endif

#endif /* DEVICE_H */
