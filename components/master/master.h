#ifndef MASTER_H
#define MASTER_H

/*
 * master.h - IoT-DMP 平台接入（MQTT 3.1.1）
 * ============================================================================
 *
 * 协议见同目录 MQTT.md，本组件在 components/mqtt 之上完成：
 *   - 鉴权密码：HMAC-MD5(model_id + "/" + device_id + "/" + model_secret,
 *     model_secret)，hex 小写；
 *   - 自动订阅 7 个下行主题（property/set、property/get、service/call、
 *     setting/set、setting/get、database/sync、database/load）；
 *   - 下行消息解析后通过回调上抛，并提供 master_reply() 生成统一响应；
 *   - 属性 / 事件 / 状态 / OTA 进度上报接口。
 *
 * JSON 处理：
 *   - 下行使用 var_from_json_ex() 解析完整对象；
 *   - 上行使用 var_t 组织参数，var_to_json() 序列化；
 *   - 不依赖 cJSON。
 *
 * 使用顺序：
 *   master_callbacks_t cbs = { ... };
 *   master_init(&cfg, &cbs);
 *   master_start();
 *   while (...) {
 *       master_tick(200);          // 收发 + 断线重连 + 心跳
 *   }
 *   master_stop();
 *
 * 约束：
 *   - 网络后端由构建目标选择：f407ve 使用 ML307 模组内置 MQTT 栈，
 *     主机程序使用 components/mqtt/mqtt.c；
 *   - 单实例、非线程安全，所有接口必须在同一任务上下文调用；
 *   - 回调中的指针只在回调执行期间有效。
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "var.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MASTER_HOST_MAX        64U     /* 域名 / IP 缓冲长度 */
#define MASTER_ID_MAX          48U     /* device_id / model_id 缓冲长度 */
#define MASTER_SECRET_MAX      64U     /* model_secret 缓冲长度 */
#define MASTER_PAYLOAD_MAX     384U    /* 下行 JSON Payload 上限（解析缓冲） */
#define MASTER_ID_STR_MAX      40U     /* 消息 id 文本长度（UUID 形） */

/* 下行操作类型 */
typedef enum {
    MASTER_CMD_PROPERTY_SET = 0,   /* 属性设置 */
    MASTER_CMD_PROPERTY_GET,       /* 属性查询 */
    MASTER_CMD_SERVICE_CALL,       /* 服务调用（含 OTA 下发） */
    MASTER_CMD_SETTING_SET,        /* 配置下发 */
    MASTER_CMD_SETTING_GET,        /* 配置查询 */
    MASTER_CMD_DATABASE_SYNC,      /* 数据库同步 */
    MASTER_CMD_DATABASE_LOAD,      /* 数据库读取 */
    MASTER_CMD_UNKNOWN
} master_cmd_t;

/* 连接参数（字符串会被拷贝，调用后无需保持有效） */
typedef struct {
    const char *host;           /* Broker 地址，必填 */
    uint16_t    port;           /* Broker 端口，0 表示 1883 */
    const char *device_id;      /* ClientID，必填 */
    const char *model_id;       /* Username，必填 */
    const char *model_secret;   /* 签名密钥，必填 */
    uint16_t    keepalive_s;    /* 0 表示 60 秒 */
    bool        clean_session;  /* 是否清理会话 */
} master_config_t;

/* 下行消息（解析结果；指针仅在回调期间有效） */
typedef struct {
    master_cmd_t cmd;           /* 操作类型 */
    const char  *topic;         /* 完整下行主题 */
    const char  *id;            /* 请求 id（无则为空串） */
    const char  *service;       /* service/call 的服务名，其他为 NULL */
    const var_t *params;        /* params/config/keys 解析结果，可能为 NULL */
    const char  *raw_json;      /* 原始 Payload（'\0' 结尾） */
    uint32_t     raw_len;
} master_msg_t;

/*
 * 下行回调（按主题类型分发）。
 * 回调内可调用 master_reply() 回复；未注册对应回调时，已知命令自动回复
 * {"id":..,"ret":1,"message":"success"}，未知主题（如 xxx/response）直接忽略。
 * msg 中的指针仅在回调期间有效。
 */
typedef void (*master_topic_cb)(const master_msg_t *msg, void *user);

typedef struct {
    master_topic_cb property_set;    /* property/set   */
    master_topic_cb property_get;    /* property/get   */
    master_topic_cb service_call;    /* service/call   */
    master_topic_cb setting_set;     /* setting/set    */
    master_topic_cb setting_get;     /* setting/get    */
    master_topic_cb database_sync;   /* database/sync  */
    master_topic_cb database_load;   /* database/load  */
    master_topic_cb unknown;         /* 其他主题（上报应答等） */
    void *user;                      /* 透传给所有回调 */
} master_callbacks_t;

/* 初始化：复制连接参数与回调表（cbs 可为 NULL） */
int  master_init(const master_config_t *cfg, const master_callbacks_t *cbs);

/* 建立连接并订阅全部下行主题；返回 0 表示连接成功且订阅全部成功 */
int  master_start(void);

/* 发送 DISCONNECT 并关闭连接 */
void master_stop(void);

/*
 * 推进一次收发：未连接时按 5 秒间隔自动重连；连接后处理一个入站报文，
 * timeout_ms 为等待时长。返回 NET_* 或 0。
 */
int  master_tick(uint32_t timeout_ms);

bool master_is_online(void);
bool master_is_inited(void);

/* 当前配置（调试用） */
const char *master_host(void);
uint16_t    master_port(void);
const char *master_device_id(void);
const char *master_model_id(void);

/* 回复下行请求：topic 自动追加 "/response"，data 可为 NULL */
int  master_reply(const master_msg_t *req, int ret, const char *message,
                  const var_t *data);

/* 向 $thing/{device_id}/<suffix> 发布原始 JSON（QoS1） */
int  master_publish_suffix(const char *suffix, const char *payload,
                           uint32_t len);

/* 上报：params 为 var 对象，可为 NULL（空对象） */
int  master_report_property(const var_t *params);
int  master_report_status(const var_t *params);

/* 事件上报：type 取 info / alert / fault */
int  master_report_event(const char *name, const char *type,
                         const var_t *params);

/* OTA 进度上报，stage 见 MQTT.md（downloading/verifying/...） */
int  master_report_ota(const char *task_id, const char *stage, int progress,
                       const char *error, const char *version);

/*
 * 计算鉴权密码：HMAC-MD5(model_id + "/" + device_id + "/" + model_secret,
 * model_secret)，输出 32 字符 hex 小写 + '\0'。
 * 返回 0 成功；-1 参数非法。
 */
int  master_auth_password(const char *model_id, const char *device_id,
                          const char *model_secret, char out[33]);

#ifdef __cplusplus
}
#endif

#endif /* MASTER_H */
