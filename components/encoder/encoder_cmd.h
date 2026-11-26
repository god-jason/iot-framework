#ifndef ENCODER_CMD_H
#define ENCODER_CMD_H

/* encoder_cmd.h - encoder 组件 roshell 命令注册 */

#include "encoder.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 把编码器实例登记到 encoder 命令（首次调用时注册命令） */
void encoder_cmd_add(encoder_t *enc);

#ifdef __cplusplus
}
#endif

#endif /* ENCODER_CMD_H */
