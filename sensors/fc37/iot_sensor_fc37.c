#include "iot_sensor_fc37.h"
#include "iot_logger.h"

int iot_fc37_init(const iot_adc_bus_t *bus)
{
    if (bus == NULL) {
        return -1;
    }
    LOG_I("fc37", "init ok");
    return 0;
}

int iot_fc37_read(const iot_adc_bus_t *bus, uint16_t *raw)
{
    uint16_t mv;

    if (bus == NULL || raw == NULL) {
        return -1;
    }
    if (iot_adc_read_mv(bus, &mv) != 0) {
        LOG_E("fc37", "adc read fail");
        return -1;
    }
    *raw = mv;   /* 原始电压(mV)，雨量换算需按实际标定 */
    return 0;
}