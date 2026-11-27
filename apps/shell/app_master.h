#ifndef APP_MASTER_H
#define APP_MASTER_H

/*
 * app_master.h - PC shell 接入 IoT-DMP 平台（components/master）的演示命令
 *
 * 注册命令：
 *   master [host] [port] [seconds]
 *       连接平台、订阅 7 个下行主题、上报状态/属性，并轮询下行
 *       （默认 47.114.32.128:1883，轮询 60 秒）。
 *
 * 与 f407ve 版本的区别：直接使用 PC 的 BSD socket（components/net），
 * 不依赖 ML307。
 */

#ifdef __cplusplus
extern "C" {
#endif

/* 注册 master 命令（不访问网络） */
void app_master_register(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_MASTER_H */
