#include "acs712.h"
#include "iot_logger.h"

/* 零电流输出基准 2.5V。 */
#define ACS712_VIO_MV 2500.0f
/* 灵敏度：20A/30A 版为 66mV/A；5A 版请改为 185.0f。电流 = (电压(mV) - 2500) / 灵敏度。 */
#define ACS712_SENS_MV_PER_A 66.0f

int acs712_init(const adc_bus_t *bus)
{
    if (bus == NULL) {
        return -1;
    }
    LOG_I("acs712", "init ok");
    return 0;
}

int acs712_read(const adc_bus_t *bus, float *amps)
{
    uint16_t mv;

    if (bus == NULL || amps == NULL) {
        return -1;
    }
    if (adc_read_mv(bus, &mv) != 0) {
        LOG_E("acs712", "adc read fail");
        return -1;
    }
    *amps = ((float)mv - ACS712_VIO_MV) / ACS712_SENS_MV_PER_A;
    return 0;
}