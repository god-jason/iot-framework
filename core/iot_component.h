#ifndef IOT_COMPONENT_H
#define IOT_COMPONENT_H

/*
 * 组件管理：统一注册、初始化、启动、处理、停止、销毁的生命周期。
 */

#include "iot_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    IOT_COMPONENT_NONE,
    IOT_COMPONENT_REGISTERED,
    IOT_COMPONENT_INITIALIZED,
    IOT_COMPONENT_STARTED,
    IOT_COMPONENT_STOPPED,
    IOT_COMPONENT_ERROR
} iot_component_state_t;

typedef struct iot_component iot_component_t;

struct iot_component {
    const char *name;
    uint16_t id;

    int (*init)(iot_component_t *self);
    int (*start)(iot_component_t *self);
    int (*process)(iot_component_t *self);
    int (*stop)(iot_component_t *self);
    int (*deinit)(iot_component_t *self);

    void *ctx;
    iot_component_state_t state;
};

int iot_component_register(iot_component_t *component);
int iot_component_unregister(const char *name);

int iot_component_init_all(void);
int iot_component_start_all(void);
int iot_component_process_all(void);
int iot_component_stop_all(void);
int iot_component_deinit_all(void);

iot_component_t *iot_component_find(const char *name);
iot_component_state_t iot_component_get_state(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* IOT_COMPONENT_H */