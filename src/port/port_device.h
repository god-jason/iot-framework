#ifndef PORT_DEVICE_H
#define PORT_DEVICE_H

/*
 * 统一设备管理：把外设句柄以名字注册到设备表，便于按名查找。
 * 基础外设 API 不依赖设备管理器。
 */

#include "port_def.h"
#include "port_platform.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct port_device port_device_t;

typedef struct {
    port_status_t (*init)(port_device_t *device);
    port_status_t (*deinit)(port_device_t *device);
    port_status_t (*control)(port_device_t *device, uint32_t command, void *argument);
} port_device_ops_t;

struct port_device {
    const char *name;

    port_device_type_t type;

    void *handle;

    const port_device_ops_t *ops;

    void *user_data;
};

port_status_t port_device_register(port_device_t *device);
port_status_t port_device_unregister(const char *name);
port_device_t *port_device_find(const char *name);

port_status_t port_device_init(port_device_t *device);
port_status_t port_device_deinit(port_device_t *device);
port_status_t port_device_control(port_device_t *device, uint32_t command, void *argument);

#ifdef __cplusplus
}
#endif

#endif /* PORT_DEVICE_H */
