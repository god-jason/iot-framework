#include "iot_sensor_gp2y1010au0f.h"
#include "iot_logger.h"

/* 洁净空气基线电压(mV)，需按实际标定。 */
#define GP2Y_VOFF_MV 600.0f
/* 灵敏度：约 0.5V 对应 0.1mg/m³。 */
#define GP2Y_SENS_MV_PER_01 500.0f

/* 简化为线性映射，省略 LED 脉冲时序；精确测量需配合脉冲采样并按标定曲线修正。 */
int iot_gp2y1010_read(const iot_adc_bus_t *bus, float *density)
{
    uint16_t mv;
    float d;

    if (bus == NULL || density == NULL) {
        return -1;
    }
    if (iot_adc_read_mv(bus, &mv) != 0) {
        LOG_E("gp2y1010", "adc read fail");
        return -1;
    }
    d = ((float)mv - GP2Y_VOFF_MV) / GP2Y_SENS_MV_PER_01 * 0.1f;
    if (d < 0.0f) {
        d = 0.0f;
    }
    *density = d;
    return 0;
}