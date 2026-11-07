#include "spi.h"

int spi_transfer(const spi_bus_t *bus, const uint8_t *tx, uint8_t *rx, uint32_t len)
{
    if (bus == NULL || bus->transfer == NULL) {
        return -1;
    }
    return bus->transfer(bus->ctx, tx, rx, len);
}

void spi_cs(const spi_bus_t *bus, bool select)
{
    if (bus != NULL && bus->cs_select != NULL) {
        bus->cs_select(bus->ctx, select);
    }
}