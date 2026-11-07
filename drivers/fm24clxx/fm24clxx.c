#include "fm24clxx.h"
#include "iot_logger.h"

#define FM24CLXX_ADDR  0x50
#define FM24CLXX_CHUNK 32  // 单次 I2C 传输最大数据字节数（不含 2 字节地址）

int fm24clxx_write(const i2c_bus_t *bus, uint16_t addr, const uint8_t *data, uint16_t len)
{
    uint8_t buf[FM24CLXX_CHUNK + 2];
    uint16_t off = addr;
    uint16_t done = 0;

    if (bus == NULL || bus->write == NULL || (len > 0 && data == NULL)) {
        return -1;
    }

    while (done < len) {
        uint16_t n = len - done;
        uint8_t i;
        if (n > FM24CLXX_CHUNK) {
            n = FM24CLXX_CHUNK;
        }
        buf[0] = (uint8_t)(off >> 8);   // 地址高字节（大端）
        buf[1] = (uint8_t)(off & 0xFF); // 地址低字节
        for (i = 0; i < (uint8_t)n; i++) {
            buf[2 + i] = data[done + i];
        }
        if (bus->write(bus->ctx, FM24CLXX_ADDR, buf, (uint32_t)n + 2) != 0) {
            return -1;
        }
        done = (uint16_t)(done + n);
        off = (uint16_t)(off + n);
    }
    return 0;
}

int fm24clxx_read(const i2c_bus_t *bus, uint16_t addr, uint8_t *data, uint16_t len)
{
    uint8_t abuf[2];
    uint16_t off = addr;
    uint16_t done = 0;

    if (bus == NULL || bus->write == NULL || bus->read == NULL || (len > 0 && data == NULL)) {
        return -1;
    }

    while (done < len) {
        uint16_t n = len - done;
        if (n > FM24CLXX_CHUNK) {
            n = FM24CLXX_CHUNK;
        }
        // 先写 2 字节地址（大端），再读 n 字节数据（FRAM 支持当前地址连续读）。
        abuf[0] = (uint8_t)(off >> 8);
        abuf[1] = (uint8_t)(off & 0xFF);
        if (bus->write(bus->ctx, FM24CLXX_ADDR, abuf, 2) != 0) {
            return -1;
        }
        if (bus->read(bus->ctx, FM24CLXX_ADDR, &data[done], n) != 0) {
            return -1;
        }
        done = (uint16_t)(done + n);
        off = (uint16_t)(off + n);
    }
    return 0;
}