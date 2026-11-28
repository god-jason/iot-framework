#ifndef FAN_CMD_H
#define FAN_CMD_H

/* fan_cmd.h - fan 组件 roshell 命令注册 */

#include "fan.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 把风扇实例登记到 fan 命令（首次调用时注册命令） */
void fan_cmd_add(fan_t *fan);

#ifdef __cplusplus
}
#endif

#endif /* FAN_CMD_H */
