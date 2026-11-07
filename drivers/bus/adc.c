#include "adc.h"

int adc_read_mv(const adc_bus_t *bus, uint16_t *mv)
{
    if (bus == NULL || bus->read_mv == NULL || mv == NULL) {
        return -1;
    }
    return bus->read_mv(bus->ctx, mv);
}