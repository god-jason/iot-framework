#include "iot_os.h"

static const iot_os_ops_t *s_os = NULL;

int iot_os_init(const iot_os_ops_t *ops)
{
    if (ops == NULL) {
        return -1;
    }
    s_os = ops;
    return IOT_OK;
}

const iot_os_ops_t *iot_os_get(void)
{
    return s_os;
}