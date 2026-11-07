#include "ntag21x.h"
#include "iot_logger.h"

#define NTAG21X_ADDR      0x55
#define NTAG21X_PAGE_SIZE 4

int ntag21x_read(const i2c_bus_t *bus, uint8_t page, uint8_t *data)
{
    if (bus == NULL || bus->write == NULL || bus->read == NULL || data == NULL) {
        return -1;
    }
    // 先写页地址（1 字节），再读该页 4 字节。
    if (bus->write(bus->ctx, NTAG21X_ADDR, &page, 1) != 0) {
        return -1;
    }
    if (bus->read(bus->ctx, NTAG21X_ADDR, data, NTAG21X_PAGE_SIZE) != 0) {
        return -1;
    }
    return 0;
}

int ntag21x_write(const i2c_bus_t *bus, uint8_t page, const uint8_t *data)
{
    uint8_t buf[1 + NTAG21X_PAGE_SIZE];
    uint8_t i;

    if (bus == NULL || bus->write == NULL || data == NULL) {
        return -1;
    }
    // 页地址后跟 4 字节页数据一并写入。
    buf[0] = page;
    for (i = 0; i < NTAG21X_PAGE_SIZE; i++) {
        buf[1 + i] = data[i];
    }
    if (bus->write(bus->ctx, NTAG21X_ADDR, buf, sizeof(buf)) != 0) {
        return -1;
    }
    return 0;
}