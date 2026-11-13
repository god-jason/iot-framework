#ifndef SHELL_NET_H
#define SHELL_NET_H

/*
 * shell_net.h - Shell 网络命令（ping）
 *
 * 命令随 Shell 实例一起注册，底层调用 components/net 的 net_ping()；
 * 具体网络后端（BSD / lwIP / ML307）由构建系统选择编译的文件。
 */

#ifdef __cplusplus
extern "C" {
#endif

/* 注册网络命令；已注册的命令不会重复添加。返回 0 成功 */
int shell_net_register(void);

#ifdef __cplusplus
}
#endif

#endif /* SHELL_NET_H */
