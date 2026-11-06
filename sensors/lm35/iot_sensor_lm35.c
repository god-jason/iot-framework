#include "iot_sensor_lm35.h"
#include "iot_logger.h"

/* 灵敏度 10mV/℃，摄氏温度 = 电压(mV) / 10。 */
#define LM35_TC_MV_PER_C 10.0f

int iot_lm35_init(const iot_adc_bus_t *bus)
{
    if (bus == NULL) {
        return -1;
    }
    LOG_I("lm35", "init ok");
    return 0;
}

int iot_lm35_read(const iot_adc_bus_t *bus, float *celsius)
{
    uint16_t mv;

    if (bus == NULL || celsius == NULL) {
        return -1;
    }
    if (iot_adc_read_mv(bus, &mv) != 0) {
        LOG_E("lm35", "adc read fail");
        return -1;
    }
    *celsius = (float)mv / LM35_TC_MV_PER_C;
    return 0;
}