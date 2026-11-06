#include "iot_drv_scd30.h"
#include "iot_logger.h"
#include <string.h>

#define SCD30_ADDR 0x61

/* 从 6 字节字段中取出 4 字节大端 float（字节 0,1,3,4，跳过 CRC） */
static float scd30_float(const uint8_t *p)
{
    uint32_t u = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
                 ((uint32_t)p[3] << 8)  | ((uint32_t)p[4]);
    float f;
    memcpy(&f, &u, sizeof(f));
    return f;
}

int iot_scd30_init(const iot_i2c_bus_t *bus)
{
    uint8_t cmd[4];

    if (bus == NULL || bus->write == NULL) {
        return -1;
    }
    /* 启动连续测量：命令 0x0010 + 压力参数 0x0000（使用默认值） */
    cmd[0] = 0x00; cmd[1] = 0x10; cmd[2] = 0x00; cmd[3] = 0x00;
    if (bus->write(bus->ctx, SCD30_ADDR, cmd, 4) != 0) {
        LOG_E("scd30", "start fail");
        return -1;
    }
    LOG_I("scd30", "init ok");
    return 0;
}

int iot_scd30_read(const iot_i2c_bus_t *bus, float *co2, float *temp, float *humidity)
{
    uint8_t cmd[2];
    uint8_t ready[3];
    uint8_t d[18];

    if (bus == NULL || bus->write == NULL || bus->read == NULL) {
        return -1;
    }
    /* 查询数据就绪 0x0202，返回 3 字节：标志(2) + CRC(1) */
    cmd[0] = 0x02; cmd[1] = 0x02;
    if (bus->write(bus->ctx, SCD30_ADDR, cmd, 2) != 0) {
        return -1;
    }
    if (bus->read(bus->ctx, SCD30_ADDR, ready, 3) != 0) {
        return -1;
    }
    /* 就绪：标志为大端 0x0001 */
    if (ready[0] != 0x00 || ready[1] != 0x01) {
        return -1;
    }

    /* 读测量结果 0x0300：18 字节，CO2/温度/湿度各为 4 字节大端 float，
     * 每个值中间夹 2 字节 CRC */
    cmd[0] = 0x03; cmd[1] = 0x00;
    if (bus->write(bus->ctx, SCD30_ADDR, cmd, 2) != 0) {
        return -1;
    }
    if (bus->read(bus->ctx, SCD30_ADDR, d, 18) != 0) {
        return -1;
    }

    if (co2 != NULL) {
        *co2 = scd30_float(&d[0]);
    }
    if (temp != NULL) {
        *temp = scd30_float(&d[6]);
    }
    if (humidity != NULL) {
        *humidity = scd30_float(&d[12]);
    }
    return 0;
}