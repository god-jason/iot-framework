#ifndef SHELL_DEV_H
#define SHELL_DEV_H

/*
 * shell_dev.h - Shell 设备命令（GPIO / PWM / 步进电机）
 *
 * 命令在 Shell 按需实例化时自动注册（打开 Shell 才占用扩展命令表），
 * 提供：
 *   gpio    : 引脚模式配置、读写、翻转、复位
 *   pwm     : 多个已绑定 PWM 通道的启停、频率 / 占空比调整与状态查询
 *             寻址方式：pwm <name> ... 或 pwm <index> ...（见 pwm list）
 *   stepper : 多实例步进电机控制，stepper <id> <子命令> ...
 *             id = 0 .. SHELL_STEPPER_MAX-1，stepper list 查看实例
 *   fan     : 多实例 PWM 风扇控制与转速读取，fan <id> <子命令> ...
 *             id = 0 .. SHELL_FAN_MAX-1，fan list 查看实例
 *
 * PWM 通道的定时器资源由平台准备（base.instance / channel 预先设置好），
 * 再通过 shell_pwm_bind() 绑定一个名字；可绑定 SHELL_PWM_MAX 个通道。
 * 风扇对象由应用初始化后通过 shell_fan_bind() 绑定实例号。
 */

#include "shell.h"
#include "hal.h"
#include "stepper.h"
#include "fan.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 注册设备命令（幂等）；返回 0 成功，-1 失败（命令表满 / 名字冲突） */
int shell_dev_register(void);

/*
 * 绑定 PWM 通道。
 * name   : Shell 中使用的名字（如 "pwm1"），长度 < SHELL_PWM_NAME_MAX
 * handle : 平台已初始化的 PWM 句柄（静态分配）
 * 返回   : 0 成功；-1 参数非法、重名或绑定表已满（SHELL_PWM_MAX）
 */
int shell_pwm_bind(const char *name, hal_pwm_t *handle);

/* 解除 PWM 绑定；返回 0 成功，-1 未找到 */
int shell_pwm_unbind(const char *name);

/*
 * 绑定风扇实例。
 * id   : 实例号 0 .. SHELL_FAN_MAX-1
 * fan  : 应用已 fan_init() 的风扇对象（静态分配）
 * 返回 : 0 成功；-1 参数非法或 id 越界
 */
int shell_fan_bind(uint8_t id, fan_t *fan);

/* 解除风扇绑定；返回 0 成功，-1 未找到 */
int shell_fan_unbind(uint8_t id);

#ifdef __cplusplus
}
#endif

#endif /* SHELL_DEV_H */
