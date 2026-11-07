#include "port_os.h"

static const port_os_ops_t *s_os = NULL;

port_status_t port_os_register(const port_os_ops_t *ops)
{
    if (ops == NULL) {
        return PORT_INVALID_PARAM;
    }
    s_os = ops;
    return PORT_OK;
}

const port_os_ops_t *port_os_get(void)
{
    return s_os;
}
