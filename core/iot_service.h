#ifndef IOT_SERVICE_H
#define IOT_SERVICE_H

/*
 * 服务管理：长期运行功能的统一生命周期（ctx 风格回调）。
 */

#include "iot_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct iot_service iot_service_t;

struct iot_service {
    const char *name;

    int (*init)(void *ctx);
    int (*start)(void *ctx);
    int (*stop)(void *ctx);
    int (*process)(void *ctx);
    int (*deinit)(void *ctx);

    void *ctx;
    uint8_t enabled;
    uint8_t state;
};

int iot_service_register(iot_service_t *service);
int iot_service_unregister(const char *name);

int iot_service_start(const char *name);
int iot_service_stop(const char *name);
int iot_service_start_all(void);
int iot_service_stop_all(void);
int iot_service_process_all(void);

iot_service_t *iot_service_find(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* IOT_SERVICE_H */