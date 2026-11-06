#include "iot_drv_bh1750.h"
#include "iot_logger.h"

#define BH1750_ADDR 0x23

int iot_bh1750_init(const iot_i2c_bus_t *bus)
{
    const uint8_t power_on = 0x01;      /* 上电 */
    const uint8_t cont_h_res = 0x10;    /* 连续高分辨率模式 */

    if (bus == NULL || bus->write == NULL) {
        return -1;
    }
    if (bus->write(bus->ctx, BH1750_ADDR, &power_on, 1) != 0 ||
        bus->write(bus->ctx, BH1750_ADDR, &cont_h_res, 1) != 0) {
        LOG_E("bh1750", "init fail");
        return -1;
    }
    LOG_I("bh1750", "init ok");
    return 0;
}

int iot_bh1750_read(const iot_i2c_bus_t *bus, float *lux)
{
    uint8_t d[2];
    uint16_t raw;

    if (bus == NULL || bus->read == NULL || lux == NULL) {
        return -1;
    }
    if (bus->read(bus->ctx, BH1750_ADDR, d, 2) != 0) {
        return -1;
    }
    raw = (uint16_t)((d[0] << 8) | d[1]);
    *lux = raw / 1.2f;
    return 0;
}