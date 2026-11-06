#include "iot_sensor_capsoil.h"
#include "iot_logger.h"

int iot_capsoil_init(const iot_adc_bus_t *bus)
{
    if (bus == NULL) {
        return -1;
    }
    LOG_I("capsoil", "init ok");
    return 0;
}

int iot_capsoil_read(const iot_adc_bus_t *bus, uint16_t *raw)
{
    uint16_t mv;

    if (bus == NULL || raw == NULL) {
        return -1;
    }
    if (iot_adc_read_mv(bus, &mv) != 0) {
        LOG_E("capsoil", "adc read fail");
        return -1;
    }
    *raw = mv;   /* 原始电压(mV)，湿度百分比需按实际标定 */
    return 0;
}