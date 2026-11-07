#include "ldr.h"
#include "iot_logger.h"

int ldr_init(const adc_bus_t *bus)
{
    if (bus == NULL) {
        return -1;
    }
    LOG_I("ldr", "init ok");
    return 0;
}

int ldr_read(const adc_bus_t *bus, uint16_t *raw)
{
    uint16_t mv;

    if (bus == NULL || raw == NULL) {
        return -1;
    }
    if (adc_read_mv(bus, &mv) != 0) {
        LOG_E("ldr", "adc read fail");
        return -1;
    }
    /* 亮度越大电压越低，返回原始电压(mV)，照度换算需按实际标定。 */
    *raw = mv;
    return 0;
}