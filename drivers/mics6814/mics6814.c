#include "mics6814.h"
#include "iot_logger.h"

/*
 * MiCS-6814 含 CO、NO2、NH3 三路模拟输出，每路需接独立 ADC 通道（即一个总线对应一路）。
 * 本 read 仅读取该总线所接通道的原始电压(mV)，三路需构造三个 adc_bus_t 分别读取。
 */
int mics6814_init(const adc_bus_t *bus)
{
    if (bus == NULL) {
        return -1;
    }
    LOG_I("mics6814", "init ok");
    return 0;
}

int mics6814_read(const adc_bus_t *bus, uint16_t *ch)
{
    uint16_t mv;

    if (bus == NULL || ch == NULL) {
        return -1;
    }
    if (adc_read_mv(bus, &mv) != 0) {
        LOG_E("mics6814", "adc read fail");
        return -1;
    }
    *ch = mv;   /* 原始电压(mV)，需按标定曲线换算 CO/NO2/NH3 浓度 */
    return 0;
}