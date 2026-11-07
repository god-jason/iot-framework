#include "dht20.h"
#include "iot_logger.h"

#define DHT20_ADDR 0x38

int dht20_init(const i2c_bus_t *bus)
{
    uint8_t cal[2] = { 0x08, 0x00 };
    uint8_t st = 0;
    const uint8_t soft_reset = 0xBA;
    const uint8_t st_reg = 0x71;
    const uint8_t cal_cmd = 0xBE;

    if (bus == NULL || bus->write == NULL || bus->read == NULL) {
        return -1;
    }
    /* 软复位命令 0xBA */
    bus->write(bus->ctx, DHT20_ADDR, &soft_reset, 1);

    /* 读状态寄存器 0x71 */
    bus->write(bus->ctx, DHT20_ADDR, &st_reg, 1);
    bus->read(bus->ctx, DHT20_ADDR, &st, 1);

    /* 未校准（bit3=0）则发起校准 */
    if (!(st & 0x08)) {
        bus->write(bus->ctx, DHT20_ADDR, &cal_cmd, 1);
        bus->write(bus->ctx, DHT20_ADDR, cal, 2);
    }
    LOG_I("dht20", "init ok");
    return 0;
}

int dht20_read(const i2c_bus_t *bus, float *temp, float *humidity)
{
    uint8_t d[7];
    uint32_t h, t;
    const uint8_t trig[3] = { 0xAC, 0x33, 0x00 };

    if (bus == NULL || bus->write == NULL || bus->read == NULL ||
        temp == NULL || humidity == NULL) {
        return -1;
    }
    /* 触发测量命令 0xAC 0x33 0x00，随后读 7 字节 */
    if (bus->write(bus->ctx, DHT20_ADDR, trig, 3) != 0) {
        return -1;
    }
    if (bus->read(bus->ctx, DHT20_ADDR, d, 7) != 0) {
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