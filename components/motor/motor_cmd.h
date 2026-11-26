#ifndef MOTOR_CMD_H
#define MOTOR_CMD_H

/* motor_cmd.h - motor 组件 roshell 命令注册 */

#include "motor.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 把电机实例登记到 motor 命令（首次调用时注册命令） */
void motor_cmd_add(motor_t *motor);

#ifdef __cplusplus
}
#endif

#endif /* MOTOR_CMD_H */
