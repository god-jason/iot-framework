#ifndef IOT_MODEM_H
#define IOT_MODEM_H

/*
 * 通用 Modem 抽象接口（与具体模组型号无关）。
 * 不同 4G 模组（ML307 / EC801 …）实现同一套 iot_modem_ops_t。
 */

#include "iot_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    IOT_MODEM_OFF = 0,
    IOT_MODEM_STARTING,
    IOT_MODEM_READY,
    IOT_MODEM_REGISTERING,
    IOT_MODEM_REGISTERED,
    IOT_MODEM_CONNECTED,
    IOT_MODEM_SLEEP,
    IOT_MODEM_ERROR
} iot_modem_state_t;

typedef struct {
    char imei[20];
    char iccid[24];
    char imsi[20];

    int16_t rssi;
    int16_t rsrp;
    int16_t rsrq;
    int16_t sinr;

    iot_modem_state_t state;

    char operator_name[32];
    char ip_address[48];
} iot_modem_info_t;

typedef struct {
    int (*init)(void *ctx);
    int (*start)(void *ctx);
    int (*stop)(void *ctx);
    int (*reset)(void *ctx);

    int (*get_info)(void *ctx, iot_modem_info_t *info);

    int (*network_connect)(void *ctx);
    int (*network_disconnect)(void *ctx);

    int (*sleep)(void *ctx);
    int (*wake)(void *ctx);

    int (*process)(void *ctx);

    void *ctx;
} iot_modem_ops_t;

#ifdef __cplusplus
}
#endif

#endif /* IOT_MODEM_H */