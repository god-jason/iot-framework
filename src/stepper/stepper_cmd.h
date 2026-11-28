#ifndef STEPPER_CMD_H
#define STEPPER_CMD_H

/* stepper_cmd.h - stepper 组件 roshell 命令注册 */

#include "stepper.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 把步进电机实例登记到 stepper 命令（首次调用时注册命令） */
void stepper_cmd_add(stepper_t *st);

#ifdef __cplusplus
}
#endif

#endif /* STEPPER_CMD_H */
