#ifndef APP_ML307_H
#define APP_ML307_H

/*
 * app_ml307.h - STM32F103CBT6 的 ML307(4G) 演示命令
 *
 * 注册以下 roshell 命令（真正的测试在会话任务里执行，栈更大）：
 *   ml307 / sim / ntp / http / net / mqtt
 */

#ifdef __cplusplus
extern "C" {
#endif

/* 注册 ML307 演示命令（首次执行时懒初始化 AT 引擎与 net 后端） */
void app_ml307_register(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_ML307_H */
