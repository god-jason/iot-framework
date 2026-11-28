#ifndef DATETIME_CMD_H
#define DATETIME_CMD_H

/*
 * datetime_cmd.h - datetime 组件 roshell 命令注册
 *
 *   date                         显示系统时间
 *   date get                     显示系统时间
 *   date set <unix>              按 Unix 秒设置
 */

#ifdef __cplusplus
extern "C" {
#endif

/* 注册 date 命令（幂等）；返回 0 成功，-1 失败 */
int datetime_cmd_register(void);

#ifdef __cplusplus
}
#endif

#endif /* DATETIME_CMD_H */
