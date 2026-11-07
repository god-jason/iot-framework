#include "port_core.h"

static const port_platform_ops_t *s_platform = NULL;

port_status_t port_platform_register(const port_platform_ops_t *ops)
{
    if (ops == NULL) {
        return PORT_INVALID_PARAM;
    }
    s_platform = ops;
    return PORT_OK;
}

const port_platform_ops_t *port_platform_get(void)
{
    return s_platform;
}

const char *port_platform_name(void)
{
    return (s_platform != NULL && s_platform->name != NULL) ? s_platform->name : "unknown";
}

port_status_t port_init(void)
{
    const port_platform_ops_t *ops;
    port_status_t status;

    if (port_platform_get() == NULL) {
        status = port_platform_install();
        if (status != PORT_OK) {
            return status;
        }
    }

    ops = port_platform_get();
    if (ops == NULL) {
        return PORT_NOT_FOUND;
    }
    return (ops->init != NULL) ? ops->init() : PORT_OK;
}

port_status_t port_deinit(void)
{
    const port_platform_ops_t *ops = port_platform_get();

    if (ops == NULL) {
        return PORT_NOT_READY;
    }
    return (ops->deinit != NULL) ? ops->deinit() : PORT_OK;
}

port_status_t port_get_info(port_mcu_info_t *info)
{
    const port_platform_ops_t *ops = port_platform_get();

    if (info == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (ops == NULL) {
        return PORT_NOT_READY;
    }
    return (ops->get_info != NULL) ? ops->get_info(info) : PORT_NOT_SUPPORTED;
}
