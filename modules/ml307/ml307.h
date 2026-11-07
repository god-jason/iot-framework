#ifndef ML307_H
#define ML307_H

/*
 * ML307 4G 模组驱动（基于 at 引擎）。
 * 只封装基础信息 / 网络注册 / 信号 / IP 等核心能力；
 * MQTT / HTTP 原语见 ml307_mqtt.h / ml307_http.h。
 */

#include "iot_types.h"
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

int ml307_init(const at_config_t *config);
int ml307_start(void);
int ml307_stop(void);
int ml307_reset(void);
int ml307_process(void);

int ml307_get_imei(char *buffer, uint32_t size);
int ml307_get_iccid(char *buffer, uint32_t size);
int ml307_get_imsi(char *buffer, uint32_t size);

int ml307_get_signal(ml307_info_t *info);

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