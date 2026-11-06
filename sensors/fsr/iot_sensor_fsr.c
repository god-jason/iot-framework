#include "iot_sensor_fsr.h"
#include "iot_logger.h"

int iot_fsr_init(const iot_adc_bus_t *bus)
{
    if (bus == NULL) {
        return -1;
    }
    LOG_I("fsr", "init ok");
    return 0;
}

int iot_fsr_read(const iot_adc_bus_t *bus, uint16_t *raw)
{
    uint16_t mv;

    if (bus == NULL || raw == NULL) {
        return -1;
    }
    if (iot_adc_read_mv(bus, &mv) != 0) {
        LOG_E("fsr", "adc read fail");
        return -1;
    }
    *raw = mv;   /* 原始电压(mV)，压力换算需按实际标定 */
    return 0;
}