#ifndef IOT_SHELL_H
#define IOT_SHELL_H

/*
 * 串口命令行（Shell）：命令注册与执行。按空格切分 argv，支持查询/执行。
 */

#include "iot_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*iot_shell_command_fn)(int argc, char **argv);

typedef struct {
    const char *name;
    const char *description;
    iot_shell_command_fn callback;
} iot_shell_command_t;

int iot_shell_init(void);

int iot_shell_register(const iot_shell_command_t *command);
int iot_shell_unregister(const char *name);

/* 喂入字节流（行以 \n 结束即执行） */
int iot_shell_input(const char *data, uint32_t len);
int iot_shell_process(void);

/* 直接执行一行命令 */
int iot_shell_execute(const char *command);

#ifdef __cplusplus
}
#endif

#endif /* IOT_SHELL_H */