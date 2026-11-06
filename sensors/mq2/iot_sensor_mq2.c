#include "iot_sensor_mq2.h"
#include "iot_logger.h"

/* 模块供电与加热电压（多数 MQ 模块为 5V 分压供电）。 */
#define MQ_VC_MV 5000U
/* 线性近似系数：ppm 约等于 K x (R0/Rs)。真实曲线为 ppm = A x (Rs/R0)^B，A、B 需按标定曲线确定。 */
#define MQ_PPM_K 1000.0f

int iot_mq2_init(const iot_adc_bus_t *bus)
{
    if (bus == NULL) {
        return -1;
    }
    LOG_I("mq2", "init ok");
    return 0;
}

int iot_mq2_read(const iot_adc_bus_t *bus, uint32_t *ppm)
{
    uint16_t mv;
    float ratio;

    if (bus == NULL || ppm == NULL) {
        return -1;
    }
    if (iot_adc_read_mv(bus, &mv) != 0) {
        LOG_E("mq2", "adc read fail");
        return -1;
    }
    if (mv >= MQ_VC_MV) {
        ratio = 1.0e4f;                              /* 接近供电电压，防除零 */
    } else {
        ratio = (float)mv / (float)(MQ_VC_MV - mv);  /* R0/Rs 近似值 */
    }
    *ppm = (uint32_t)(MQ_PPM_K * ratio);             /* 近似值，需按实际标定 */
    return 0;
}