#ifndef IOT_MQTT_H
#define IOT_MQTT_H

/*
 * MQTT 客户端接口（单网络实现）。
 *
 * 本文件只定义类型与接口，不做运行期后端注册；
 * 具体实现在 networks/ 下按目标选择性编译：
 *   - networks/eth    : 基于系统 socket，使用 Mongoose 实现；
 *   - networks/ml307 : 基于 AT 指令，使用模组内置 MQTT 能力。
 *
 * 约定：
 *   - 同一目标只编译一个网络实现；
 *   - 配置中的字符串只在使用期间引用，实现不接管其生命周期；
 *   - 收到消息时由实现通过 iot_mqtt_process() 或内部机制回调。
 */

#include "hal_def.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 主题最大长度（含结尾 '\0'） */
#define IOT_MQTT_TOPIC_MAX   192

/* MQTT 连接配置 */
typedef struct {
    const char *host;           /* 服务器地址，必填 */
    uint16_t port;              /* 服务器端口 */

    const char *client_id;      /* 客户端 ID，可为 NULL */
    const char *username;       /* 用户名，可为 NULL */
    const char *password;       /* 密码，可为 NULL */

    uint16_t keepalive;         /* 保活周期（秒），0 表示不保活 */
    bool clean_session;         /* true = 清理会话 */
    bool ssl;                   /* true = 使用 TLS */
} iot_mqtt_config_t;

/* MQTT 消息 */
typedef struct {
    char topic[IOT_MQTT_TOPIC_MAX];  /* 主题 */

    const uint8_t *payload;     /* 载荷，可为 NULL */
    uint32_t payload_len;       /* 载荷长度 */

    uint8_t qos;                /* QoS 等级：0 或 1 */
    bool retain;                /* 保留标志 */
} iot_mqtt_message_t;

/*
 * 消息回调。
 * message   : 收到的消息，回调返回后指针即失效，需自行拷贝；
 * user_data : 注册时绑定的上下文，可为 NULL。
 */
typedef void (*iot_mqtt_message_cb_t)(const iot_mqtt_message_t *message,
                                       void *user_data);

/* 初始化并注册消息回调 */
int iot_mqtt_init(iot_mqtt_message_cb_t callback, void *user_data);

/* 反初始化，释放实现资源 */
int iot_mqtt_deinit(void);

/* 连接服务器（阻塞直到 CONNACK 或超时） */
int iot_mqtt_connect(const iot_mqtt_config_t *config);

/* 断开连接 */
int iot_mqtt_disconnect(void);

/* 订阅主题 */
int iot_mqtt_subscribe(const char *topic, uint8_t qos);

/* 取消订阅 */
int iot_mqtt_unsubscribe(const char *topic);

/* 发布消息 */
int iot_mqtt_publish(const iot_mqtt_message_t *message);

/* 周期处理（泵取收包、保活），未实现时直接返回 HAL_OK */
int iot_mqtt_process(void);

/* 查询连接状态 */
bool iot_mqtt_is_connected(void);

#ifdef __cplusplus
}
#endif

#endif /* IOT_MQTT_H */
