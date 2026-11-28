#ifndef SERVO_CMD_H
#define SERVO_CMD_H

/* servo_cmd.h - servo 组件 roshell 命令注册 */

#include "servo.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 把舵机实例登记到 servo 命令（首次调用时注册命令） */
void servo_cmd_add(servo_t *servo);

#ifdef __cplusplus
}
#endif

#endif /* SERVO_CMD_H */
