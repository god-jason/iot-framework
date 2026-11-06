#ifndef IOT_ML307_H
#define IOT_ML307_H

/*
 * ML307 4G 模组驱动（基于 iot_at 引擎）。
 * 只封装基础信息 / 网络注册 / 信号 / IP 等核心能力；
 * MQTT / HTTP 原语见 iot_ml307_mqtt.h / iot_ml307_http.h。
 */

#include "iot_types.h"
#include "iot_modem.h"
#include "iot_at.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char apn[64];
    char username[64];
    char password[64];
    uint8_t auth_type;
} iot_pdp_config_t;

int iot_ml307_init(const iot_at_config_t *config);
int iot_ml307_start(void);
int iot_ml307_stop(void);
int iot_ml307_reset(void);
int iot_ml307_process(void);

int iot_ml307_get_imei(char *buffer, uint32_t size);
int iot_ml307_get_iccid(char *buffer, uint32_t size);
int iot_ml307_get_imsi(char *buffer, uint32_t size);

int iot_ml307_get_signal(iot_modem_info_t *info);

iot_modem_state_t iot_ml307_get_state(void);

/* ---- 网络 ---- */
int iot_ml307_network_connect(const iot_pdp_config_t *config);
int iot_ml307_network_disconnect(void);

bool iot_ml307_network_is_registered(void);
bool iot_ml307_network_is_connected(void);

int iot_ml307_get_ip(char *buffer, uint32_t size);

#ifdef __cplusplus
}
#endif

#endif /* IOT_ML307_H */