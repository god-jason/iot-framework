#include "fc28.h"
#include "iot_logger.h"

int fc28_init(const adc_bus_t *bus)
{
    if (bus == NULL) {
        return -1;
    }
    LOG_I("fc28", "init ok");
    return 0;
}

int fc28_read(const adc_bus_t *bus, uint16_t *raw)
{
    uint16_t mv;

    if (bus == NULL || raw == NULL) {
        return -1;
    }
    if (adc_read_mv(bus, &mv) != 0) {
        LOG_E("fc28", "adc read fail");
        return -1;
    }
    *raw = mv;   /* 原始电压(mV)，湿度百分比需按实际标定 */
    return 0;
}