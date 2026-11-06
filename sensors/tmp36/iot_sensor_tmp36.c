#include "iot_sensor_tmp36.h"
#include "iot_logger.h"

/* 灵敏度 10mV/℃，0℃ 输出 500mV，摄氏温度 = (电压(mV) - 500) / 10。 */
#define TMP36_OFF_MV      500.0f
#define TMP36_TC_MV_PER_C 10.0f

int iot_tmp36_init(const iot_adc_bus_t *bus)
{
    if (bus == NULL) {
        return -1;
    }
    LOG_I("tmp36", "init ok");
    return 0;
}

int iot_tmp36_read(const iot_adc_bus_t *bus, float *celsius)
{
    uint16_t mv;

    if (bus == NULL || celsius == NULL) {
        return -1;
    }
    if (iot_adc_read_mv(bus, &mv) != 0) {
        LOG_E("tmp36", "adc read fail");
        return -1;
    }
    *celsius = ((float)mv - TMP36_OFF_MV) / TMP36_TC_MV_PER_C;
    return 0;
}