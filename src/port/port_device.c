#include "port_device.h"

#include <string.h>

#ifndef PORT_DEVICE_MAX
#define PORT_DEVICE_MAX 32
#endif

static port_device_t *s_devices[PORT_DEVICE_MAX];

port_status_t port_device_register(port_device_t *device)
{
    int i;

    if (device == NULL || device->name == NULL) {
        return PORT_INVALID_PARAM;
    }
    for (i = 0; i < PORT_DEVICE_MAX; i++) {
        if (s_devices[i] == device || (s_devices[i] != NULL && s_devices[i]->name != NULL &&
                                       strcmp(s_devices[i]->name, device->name) == 0)) {
            return PORT_ALREADY_INIT;
        }
    }
    for (i = 0; i < PORT_DEVICE_MAX; i++) {
        if (s_devices[i] == NULL) {
            s_devices[i] = device;
            return PORT_OK;
        }
    }
    return PORT_NO_MEMORY;
}

port_status_t port_device_unregister(const char *name)
{
    int i;

    if (name == NULL) {
        return PORT_INVALID_PARAM;
    }
    for (i = 0; i < PORT_DEVICE_MAX; i++) {
        if (s_devices[i] != NULL && s_devices[i]->name != NULL &&
            strcmp(s_devices[i]->name, name) == 0) {
            s_devices[i] = NULL;
            return PORT_OK;
        }
    }
    return PORT_NOT_FOUND;
}

port_device_t *port_device_find(const char *name)
{
    int i;

    if (name == NULL) {
        return NULL;
    }
    for (i = 0; i < PORT_DEVICE_MAX; i++) {
        if (s_devices[i] != NULL && s_devices[i]->name != NULL &&
            strcmp(s_devices[i]->name, name) == 0) {
            return s_devices[i];
        }
    }
    return NULL;
}

port_status_t port_device_init(port_device_t *device)
{
    if (device == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (device->ops == NULL || device->ops->init == NULL) {
        return PORT_NOT_SUPPORTED;
    }
    return device->ops->init(device);
}

port_status_t port_device_deinit(port_device_t *device)
{
    if (device == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (device->ops == NULL || device->ops->deinit == NULL) {
        return PORT_NOT_SUPPORTED;
    }
    return device->ops->deinit(device);
}

port_status_t port_device_control(port_device_t *device, uint32_t command, void *argument)
{
    if (device == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (device->ops == NULL || device->ops->control == NULL) {
        return PORT_NOT_SUPPORTED;
    }
    return device->ops->control(device, command, argument);
}
