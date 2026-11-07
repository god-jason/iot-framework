#ifndef ML307_H
#define ML307_H

/*
 * ML307 4G 模组驱动（基于 at 引擎）。
 * 只封装基础信息 / 网络注册 / 信号 / IP 等核心能力；
 * MQTT / HTTP 原语见 ml307_mqtt.h / ml307_http.h。
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "at.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ML307_STATE_OFF = 0,
    ML307_STATE_STARTING,
    ML307_STATE_READY,
    ML307_STATE_REGISTERING,
    ML307_STATE_REGISTERED,
    ML307_STATE_CONNECTED,
    ML307_STATE_SLEEP,
    ML307_STATE_ERROR
} ml307_state_t;

typedef struct {
    char imei[20];
    char iccid[24];
    char imsi[20];

    int16_t rssi;
    int16_t rsrp;
    int16_t rsrq;
    int16_t sinr;

    ml307_state_t state;

    char operator_name[32];
    char ip_address[48];
} ml307_info_t;

typedef struct {
    char apn[64];
    char username[64];
    char password[64];
    uint8_t auth_type;
} pdp_config_t;

/* WiFi 扫描到的一个热点 */
typedef struct {
    char ssid[33];      /* 热点名称 */
    char bssid[18];     /* MAC 地址，形如 aa:bb:cc:dd:ee:ff */
    int16_t rssi;       /* 信号强度（dBm） */
    uint8_t channel;    /* 信道号 */
} ml307_wifi_ap_t;

int ml307_init(const at_config_t *config);
int ml307_start(void);
int ml307_stop(void);
int ml307_reset(void);
int ml307_process(void);

int ml307_get_imei(char *buffer, uint32_t size);
int ml307_get_iccid(char *buffer, uint32_t size);
int ml307_get_imsi(char *buffer, uint32_t size);

int ml307_get_signal(ml307_info_t *info);

/*
 * 获取 CSQ 原始信号质量。
 * 参数：csq - 0..31（99 表示未知）；ber - 误码率 0..99，可为 NULL。
 * 返回：0 成功；-1 AT 失败或响应格式错误。
 */
int ml307_get_csq(int16_t *csq, int16_t *ber);

/*
 * 设置 PDP APN。
 * 参数：apn - 接入点名称；username/password - 可选鉴权信息（可为 NULL）。
 * 返回：0 成功；-1 参数非法（含引号或超长）或 AT 执行失败。
 */
int ml307_set_apn(const char *apn, const char *username, const char *password);

/*
 * 获取周边 WiFi 列表（需模组固件支持 AT+WIFISCAN）。
 * 参数：aps - 输出数组；max_count - 数组容量；count - 返回实际条数。
 * 返回：0 成功；-1 参数非法或 AT 执行失败。
 */
int ml307_get_wifi_list(ml307_wifi_ap_t *aps, uint8_t max_count, uint8_t *count);

ml307_state_t ml307_get_state(void);

/* ---- 网络 ---- */
int ml307_network_connect(const pdp_config_t *config);
int ml307_network_disconnect(void);

bool ml307_network_is_registered(void);
bool ml307_network_is_connected(void);

int ml307_get_ip(char *buffer, uint32_t size);

#ifdef __cplusplus
}
#endif

#endif /* ML307_H */