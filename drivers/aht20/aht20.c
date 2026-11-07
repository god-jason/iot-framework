#include "aht20.h"
#include "iot_logger.h"

#define AHT20_ADDR 0x38

int aht20_init(const i2c_bus_t *bus)
{
    uint8_t cal[2] = { 0x08, 0x00 };
    uint8_t st = 0;

    if (bus == NULL || bus->write == NULL || bus->read == NULL) {
        return -1;
    }
    /* 软复位命令 0xBA */
    const uint8_t soft_reset = 0xBA;
    bus->write(bus->ctx, AHT20_ADDR, &soft_reset, 1);

    /* 读状态寄存器 0x71 */
    bus->write(bus->ctx, AHT20_ADDR, (const uint8_t[]){0x71}, 1);
    bus->read(bus->ctx, AHT20_ADDR, &st, 1);

    /* 未校准（bit3=0）则发起校准 */
    if (!(st & 0x08)) {
        bus->write(bus->ctx, AHT20_ADDR, (const uint8_t[]){0xBE}, 1);
        bus->write(bus->ctx, AHT20_ADDR, cal, 2);
    }
    LOG_I("aht20", "init ok");
    return 0;
}

int aht20_trigger(const i2c_bus_t *bus)
{
    uint8_t cmd[2] = { 0x33, 0x00 };

    if (bus == NULL || bus->write == NULL) {
        return -1;
    }
    /* 测量命令 0xAC 0x33 0x00 */
    bus->write(bus->ctx, AHT20_ADDR, (const uint8_t[]){0xAC}, 1);
    return bus->write(bus->ctx, AHT20_ADDR, cmd, 2);
}

int aht20_read(const i2c_bus_t *bus, float *temp, float *humidity)
{
    uint8_t d[7];
    uint32_t h, t;

    if (bus == NULL || bus->read == NULL || temp == NULL || humidity == NULL) {
        return -1;
    }
    if (bus->read(bus->ctx, AHT20_ADDR, d, 7) != 0) {
        return -1;
    }
    /* 20 位湿度：d[1..3] */
    h = ((uint32_t)d[1] << 12) | ((uint32_t)d[2] << 4) | (d[3] >> 4);
    /* 20 位温度：d[3..5] */
    t = ((uint32_t)(d[3] & 0x0F) << 16) | ((uint32_t)d[4] << 8) | d[5];

    *humidity = (float)h / 1048576.0f * 100.0f;
    *temp = (float)t / 1048576.0f * 200.0f - 50.0f;
    return 0;
}