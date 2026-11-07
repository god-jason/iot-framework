#include "iot_device.h"
#include "iot_string.h"

#define IOT_DEVICE_MAX 16

static iot_device_t *s_devices[IOT_DEVICE_MAX];
static uint16_t s_next_id = 0;

iot_device_t *iot_device_find(const char *name)
{
    int i;
    if (name == NULL) {
        return NULL;
    }
    for (i = 0; i < IOT_DEVICE_MAX; i++) {
        if (s_devices[i] != NULL &&
            iot_string_compare(s_devices[i]->name, name) == 0) {
            return s_devices[i];
        }
    }
    return NULL;
}

int iot_device_register(iot_device_t *device)
{
    int i;
    if (device == NULL || device->name == NULL) {
        return -1;
    }
    for (i = 0; i < IOT_DEVICE_MAX; i++) {
        if (s_devices[i] == NULL) {
            s_devices[i] = device;
            break;
        }
    }
    if (i == IOT_DEVICE_MAX) {
        return -1;
    }
    device->id = s_next_id++;
    device->state = IOT_DEVICE_OFFLINE;
    if (device->ops != NULL && device->ops->init != NULL) {
        if (device->ops->init(device->ctx) != IOT_OK) {
            device->state = IOT_DEVICE_ERROR;
        }
    }
    return IOT_OK;
}

int iot_device_unregister(const char *name)
{
    int i;
    if (name == NULL) {
        return -1;
    }
    for (i = 0; i < IOT_DEVICE_MAX; i++) {
        if (s_devices[i] != NULL &&
            iot_string_compare(s_devices[i]->name, name) == 0) {
            s_devices[i] = NULL;
            return IOT_OK;
        }
    }
    return -1;
}

int iot_device_open(const char *name)
{
    iot_device_t *d = iot_device_find(name);
    if (d == NULL) {
        return -1;
    }
    if (d->ops != NULL && d->ops->open != NULL) {
        if (d->ops->open(d->ctx) != IOT_OK) {
            d->state = IOT_DEVICE_ERROR;
            return -1;
        }
    }
    d->state = IOT_DEVICE_ONLINE;
    return IOT_OK;
}

int iot_device_close(const char *name)
{
    iot_device_t *d = iot_device_find(name);
    if (d == NULL) {
        return -1;
    }
    if (d->ops != NULL && d->ops->close != NULL) {
        d->ops->close(d->ctx);
    }
    d->state = IOT_DEVICE_OFFLINE;
    return IOT_OK;
}

int iot_device_read(const char *name, void *data, uint32_t size)
{
    iot_device_t *d = iot_device_find(name);
    if (d == NULL) {
        return -1;
    }
    if (d->ops == NULL || d->ops->read == NULL) {
        return -1;
    }
    return d->ops->read(d->ctx, data, size);
}

int iot_device_write(const char *name, const void *data, uint32_t size)
{
    iot_device_t *d = iot_device_find(name);
    if (d == NULL) {
        return -1;
    }
    if (d->ops == NULL || d->ops->write == NULL) {
        return -1;
    }
    return d->ops->write(d->ctx, data, size);
}

int iot_device_control(const char *name, uint32_t cmd, void *arg)
{
    iot_device_t *d = iot_device_find(name);
    if (d == NULL) {
        return -1;
    }
    if (d->ops == NULL || d->ops->control == NULL) {
        return -1;
    }
    return d->ops->control(d->ctx, cmd, arg);
}