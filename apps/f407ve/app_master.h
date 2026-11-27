#ifndef APP_MASTER_H
#define APP_MASTER_H

/*
 * app_master.h - F407VE 接入 IoT-DMP 平台（components/master）的演示命令
 *
 * 注册命令：
 *   master [host] [port] [seconds]
 *       连接平台、订阅 7 个下行主题、上报状态/属性，并轮询下行
 *       （host/port 缺省取 app_config 中的平台参数，轮询默认 60 秒）。
 *
 * 平台连接参数定义与持久化见 app_config.h（APP_MQTT_CFG /
 * app_config_mqtt()），本模块只负责连接与业务演示。
 *
 * 命令首次执行时会先通过 app_ml307_ensure() 完成模组与网络初始化。
 */

#ifdef __cplusplus
extern "C" {
#endif

/* 注册 master / bigpub 命令（不访问硬件） */
void app_master_register(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_MASTER_H */
