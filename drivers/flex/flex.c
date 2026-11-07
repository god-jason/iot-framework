#include "flex.h"
#include "iot_logger.h"

int flex_init(const adc_bus_t *bus)
{
    if (bus == NULL) {
        return -1;
    }
    LOG_I("flex", "init ok");
    return 0;
}

int flex_read(const adc_bus_t *bus, uint16_t *raw)
{
    uint16_t mv;

    if (bus == NULL || raw == NULL) {
        return -1;
    }
    if (adc_read_mv(bus, &mv) != 0) {
        LOG_E("flex", "adc read fail");
        return -1;
    }
    *raw = mv;   /* 原始电压(mV)，弯曲角度换算需按实际标定 */
    return 0;
}