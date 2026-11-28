#ifndef NET_CMD_H
#define NET_CMD_H

/*
 * net_cmd.h - net 组件 roshell 命令注册（ping）
 */

#ifdef __cplusplus
extern "C" {
#endif

/* 注册 net 相关命令（ping）到 roshell 全局命令表；由 net_init 调用 */
void net_register_commands(void);

#ifdef __cplusplus
}
#endif

#endif /* NET_CMD_H */
