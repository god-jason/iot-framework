#include "onewire.h"

int onewire_reset(const onewire_bus_t *bus)
{
    uint8_t presence = 0;

    if (bus == NULL || bus->set_level == NULL || bus->get_level == NULL) {
        return -1;
    }
    bus->set_level(bus->ctx, 0);
    bus->udelay(bus->ctx, 480);
    bus->set_level(bus->ctx, 1);
    bus->udelay(bus->ctx, 70);
    presence = bus->get_level(bus->ctx);
    bus->udelay(bus->ctx, 410);
    return (presence == 0) ? 0 : -2;   /* 0 表示检测到设备 */
}

void onewire_write_byte(const onewire_bus_t *bus, uint8_t b)
{
    int i;
    for (i = 0; i < 8; i++) {
        bus->set_level(bus->ctx, 0);
        bus->udelay(bus->ctx, 2);
        if (b & 0x01) {
            bus->set_level(bus->ctx, 1);
        }
        bus->udelay(bus->ctx, 60);
        bus->set_level(bus->ctx, 1);
        bus->udelay(bus->ctx, 2);
        b >>= 1;
    }
}

uint8_t onewire_read_byte(const onewire_bus_t *bus)
{
    uint8_t b = 0;
    int i;
    for (i = 0; i < 8; i++) {
        bus->set_level(bus->ctx, 0);
        bus->udelay(bus->ctx, 2);
        bus->set_level(bus->ctx, 1);
        bus->udelay(bus->ctx, 10);
        b >>= 1;
        if (bus->get_level(bus->ctx)) {
            b |= 0x80;
        }
        bus->udelay(bus->ctx, 55);
    }
    return b;
}