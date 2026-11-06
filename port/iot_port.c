#include "iot_port.h"

static const iot_port_t *s_port = NULL;

int iot_port_init(const iot_port_t *port)
{
    if (port == NULL) {
        return IOT_ERR_PARAM;
    }
    s_port = port;
    if (port->system.init != NULL) {
        port->system.init();
    }
    return IOT_OK;
}

const iot_port_t *iot_port_get(void)
{
    return s_port;
}