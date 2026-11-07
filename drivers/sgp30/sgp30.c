#include "sgp30.h"
#include "iot_logger.h"

#define SGP30_ADDR 0x58

int sgp30_init(const i2c_bus_t *bus)
{
    uint8_t cmd[2] = { 0x20, 0x03 };   /* init_air_quality */

    if (bus == NULL || bus->write == NULL) {
        return -1;
    }
    if (bus->write(bus->ctx, SGP30_ADDR, cmd, 2) != 0) {
        LOG_E("sgp30", "init fail");
        return -1;
    }
    LOG_I("sgp30", "init ok");
    return 0;
}

int sgp30_measure(const i2c_bus_t *bus, uint16_t *co2, uint16_t *tvoc)
{
    uint8_t cmd[2] = { 0x20, 0x08 };   /* measure_air_quality */
    uint8_t d[6];

    if (bus == NULL || bus->write == NULL || bus->read == NULL ||
        co2 == NULL || tvoc == NULL) {
        return -1;
    }
    if (bus->write(bus->ctx, SGP30_ADDR, cmd, 2) != 0) {
        return -1;
    }
    if (bus->read(bus->ctx, SGP30_ADDR, d, 6) != 0) {
        return -1;
    }
    *co2 = (uint16_t)((d[0] << 8) | d[1]);
    *tvoc = (uint16_t)((d[3] << 8) | d[4]);
    return 0;
}