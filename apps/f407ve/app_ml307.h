#ifndef APP_ML307_H
#define APP_ML307_H

/*
 * app_ml307.h - STM32F407VET6 的 ML307(4G) 演示命令
 *
 * 注册以下 roshell 命令：
 *   ml307                完整测试：SIM -> ping -> http -> tcp -> mqtt
 *   sim                  ICCID / IMEI / IMSI / CSQ / 注册状态 / IP
 *   ping <host>          模组 MPING
 *   http [url]           HTTP GET
 *   net [host] [port]    TCP 连接/发送/接收
 *   mqtt [host] [port]   MQTT 连接/订阅/发布/接收
 *
 * AT 引擎、模组握手与 net 后端在首次执行命令时惰性初始化。
 */

#ifdef __cplusplus
extern "C" {
#endif

/* 注册 ML307 演示命令（不访问硬件） */
void app_ml307_register(void);

/*
 * 惰性初始化 AT 引擎 / 模组握手 / net 后端。
 * 其它模块（如 master）在访问网络前应先调用本函数；已初始化时直接返回 0。
 * 返回 0 成功；-1 失败。
 */
int app_ml307_ensure(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_ML307_H */
