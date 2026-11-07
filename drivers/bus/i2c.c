#include "i2c.h"

#include <string.h>

int i2c_write_reg(const i2c_bus_t *bus, uint8_t dev, uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    if (bus == NULL || bus->write == NULL) {
        return -1;
    }
    return bus->write(bus->ctx, dev, buf, 2);
}

int i2c_write_regs(const i2c_bus_t *bus, uint8_t dev, uint8_t reg,
                       const uint8_t *data, uint32_t len)
{
    uint8_t buf[64];
    if (bus == NULL || bus->write == NULL || data == NULL || len + 1 > sizeof(buf)) {
        return -1;
    }
    buf[0] = reg;
    memcpy(&buf[1], data, len);
    return bus->write(bus->ctx, dev, buf, len + 1);
}

int i2c_read_reg(const i2c_bus_t *bus, uint8_t dev, uint8_t reg, uint8_t *val)
{
    if (bus == NULL || bus->write == NULL || bus->read == NULL || val == NULL) {
        return -1;
    }
    if (bus->write(bus->ctx, dev, &reg, 1) != 0) {
        return -1;
    }
    return bus->read(bus->ctx, dev, val, 1);
}

int i2c_read_regs(const i2c_bus_t *bus, uint8_t dev, uint8_t reg,
                      uint8_t *data, uint32_t len)
{
    if (bus == NULL || bus->write == NULL || bus->read == NULL || data == NULL) {
        return -1;
    }
    if (bus->write(bus->ctx, dev, &reg, 1) != 0) {
        return -1;
    }
    return bus->read(bus->ctx, dev, data, len);
}