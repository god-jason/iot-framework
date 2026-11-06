#ifndef IOT_DEVICE_H
#define IOT_DEVICE_H

/*
 * 设备管理：把 GPIO、继电器、传感器、电机等抽象为统一的设备对象。
 */

#include "iot_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    IOT_DEVICE_UNKNOWN,
    IOT_DEVICE_OFFLINE,
    IOT_DEVICE_ONLINE,
    IOT_DEVICE_ERROR
} iot_device_state_t;

typedef struct iot_device_ops {
    int (*init)(void *ctx);
    int (*open)(void *ctx);
    int (*close)(void *ctx);

    int (*read)(void *ctx, void *data, uint32_t size);
    int (*write)(void *ctx, const void *data, uint32_t size);

    int (*control)(void *ctx, uint32_t cmd, void *arg);
} iot_device_ops_t;

typedef struct iot_device {
    const char *name;
    uint16_t type;
    uint16_t id;

    const iot_device_ops_t *ops;

    void *ctx;
    iot_device_state_t state;
} iot_device_t;

int iot_device_register(iot_device_t *device);
int iot_device_unregister(const char *name);

int iot_device_open(const char *name);
int iot_device_close(const char *name);

int iot_device_read(const char *name, void *data, uint32_t size);
int iot_device_write(const char *name, const void *data, uint32_t size);

int iot_device_control(const char *name, uint32_t cmd, void *arg);

iot_device_t *iot_device_find(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* IOT_DEVICE_H */