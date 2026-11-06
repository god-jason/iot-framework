#include "iot_sensor_sht30.h"
#include "iot_logger.h"

#define SHT30_ADDR 0x44

/* 单次测量、时钟展宽、中重复率：0x2C 0x06 */
static const uint8_t SHT30_MEAS_CMD[2] = { 0x2C, 0x06 };
/* 软复位命令：0x30 0xA2 */
static const uint8_t SHT30_RESET_CMD[2] = { 0x30, 0xA2 };

int iot_sht30_init(const iot_i2c_bus_t *bus)
{
    if (bus == NULL || bus->write == NULL) {
        return -1;
    }
    if (bus->write(bus->ctx, SHT30_ADDR, SHT30_RESET_CMD, 2) != 0) {
        LOG_E("sht30", "reset fail");
        return -1;
    }
    LOG_I("sht30", "init ok");
    return 0;
}

int iot_sht30_read(const iot_i2c_bus_t *bus, float *temp, float *humidity)
{
    uint8_t d[6];
    uint16_t raw_t, raw_h;

    if (bus == NULL || bus->write == NULL || bus->read == NULL ||
        temp == NULL || humidity == NULL) {
        return -1;
    }
    if (bus->write(bus->ctx, SHT30_ADDR, SHT30_MEAS_CMD, 2) != 0) {
        return -1;
    }
    if (bus->read(bus->ctx, SHT30_ADDR, d, 6) != 0) {
        return -1;
    }
    /* 忽略温度 CRC（d[2]）与湿度 CRC（d[5]） */
    raw_t = (uint16_t)((d[0] << 8) | d[1]);
    raw_h = (uint16_t)((d[3] << 8) | d[4]);

    *temp = -45.0f + 175.0f * (float)raw_t / 65535.0f;
    *humidity = 100.0f * (float)raw_h / 65535.0f;
    return 0;
}