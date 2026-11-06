#include "iot_bus_spi.h"

int iot_spi_transfer(const iot_spi_bus_t *bus, const uint8_t *tx, uint8_t *rx, uint32_t len)
{
    if (bus == NULL || bus->transfer == NULL) {
        return -1;
    }
    return bus->transfer(bus->ctx, tx, rx, len);
}

void iot_spi_cs(const iot_spi_bus_t *bus, bool select)
{
    if (bus != NULL && bus->cs_select != NULL) {
        bus->cs_select(bus->ctx, select);
    }
}