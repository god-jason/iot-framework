#include "iot_sensor_bmp180.h"
#include "iot_logger.h"

#define BMP180_ADDR    0x77
#define BMP180_REG_CAL 0xAA   /* 校准系数起始，共 22 字节 */
#define BMP180_REG_CTL 0xF4   /* 测量控制 */
#define BMP180_REG_OUT 0xF6   /* 测量结果（MSB 在前） */

#define BMP180_CMD_T   0x2E   /* 读温度 UT */
#define BMP180_CMD_P   0x34   /* 读压力 UP，oss = 0 */

/* 校准系数：22 字节 = 11 个 16bit 大端系数 */
static int16_t  bmp180_ac1, bmp180_ac2, bmp180_ac3, bmp180_b1, bmp180_b2;
static int16_t  bmp180_mb, bmp180_mc, bmp180_md;
static uint16_t bmp180_ac4, bmp180_ac5, bmp180_ac6;
static int      bmp180_calib = 0;

int iot_bmp180_init(const iot_i2c_bus_t *bus)
{
    uint8_t cal[22];

    if (bus == NULL) {
        return -1;
    }
    if (iot_i2c_read_regs(bus, BMP180_ADDR, BMP180_REG_CAL, cal, 22) != 0) {
        LOG_E("bmp180", "cal read fail");
        return -1;
    }
    bmp180_ac1 = (int16_t)((cal[0] << 8) | cal[1]);
    bmp180_ac2 = (int16_t)((cal[2] << 8) | cal[3]);
    bmp180_ac3 = (int16_t)((cal[4] << 8) | cal[5]);
    bmp180_ac4 = (uint16_t)((cal[6] << 8) | cal[7]);
    bmp180_ac5 = (uint16_t)((cal[8] << 8) | cal[9]);
    bmp180_ac6 = (uint16_t)((cal[10] << 8) | cal[11]);
    bmp180_b1  = (int16_t)((cal[12] << 8) | cal[13]);
    bmp180_b2  = (int16_t)((cal[14] << 8) | cal[15]);
    bmp180_mb  = (int16_t)((cal[16] << 8) | cal[17]);
    bmp180_mc  = (int16_t)((cal[18] << 8) | cal[19]);
    bmp180_md  = (int16_t)((cal[20] << 8) | cal[21]);

    bmp180_calib = 1;
    LOG_I("bmp180", "init ok");
    return 0;
}

int iot_bmp180_read(const iot_i2c_bus_t *bus, float *temp, float *pressure)
{
    uint8_t d[3];
    int32_t ut, up;
    int32_t x1, x2, x3, b3, b5, b6, p;
    uint32_t b4, b7;

    if (bus == NULL || temp == NULL || pressure == NULL || bmp180_calib == 0) {
        return -1;
    }

    /* 读温度：真实硬件需等待约 4.5ms 转换，此处简化直接读取 */
    if (iot_i2c_write_reg(bus, BMP180_ADDR, BMP180_REG_CTL, BMP180_CMD_T) != 0) {
        return -1;
    }
    if (iot_i2c_read_regs(bus, BMP180_ADDR, BMP180_REG_OUT, d, 2) != 0) {
        return -1;
    }
    ut = (int32_t)((d[0] << 8) | d[1]);

    /* 读压力：oss = 0 时三字节结果右移 8 位，真实硬件需等待 */
    if (iot_i2c_write_reg(bus, BMP180_ADDR, BMP180_REG_CTL, BMP180_CMD_P) != 0) {
        return -1;
    }
    if (iot_i2c_read_regs(bus, BMP180_ADDR, BMP180_REG_OUT, d, 3) != 0) {
        return -1;
    }
    up = (((int32_t)d[0] << 16) | ((int32_t)d[1] << 8) | (int32_t)d[2]) >> 8;

    /* 温度补偿（0.1℃ 单位） */
    x1 = ((ut - (int32_t)bmp180_ac6) * (int32_t)bmp180_ac5) >> 15;
    x2 = ((int32_t)bmp180_mc << 11) / (x1 + (int32_t)bmp180_md);
    b5 = x1 + x2;
    *temp = (float)(b5 + 8) / 160.0f;

    /* 压力补偿（结果单位 Pa） */
    b6 = b5 - 4000;
    x1 = ((int32_t)bmp180_b2 * ((b6 * b6) >> 12)) >> 11;
    x2 = ((int32_t)bmp180_ac2 * b6) >> 11;
    x3 = x1 + x2;
    b3 = ((((int32_t)bmp180_ac1 * 4) + x3) + 2) >> 2;
    x1 = ((int32_t)bmp180_ac3 * b6) >> 13;
    x2 = ((int32_t)bmp180_b1 * ((b6 * b6) >> 12)) >> 16;
    x3 = ((x1 + x2) + 2) >> 2;
    b4 = ((uint32_t)bmp180_ac4 * (uint32_t)(x3 + 32768)) >> 15;
    b7 = ((uint32_t)(up - b3) * 50000UL);   /* oss = 0，故 50000 >> 0 */

    if (b7 < 0x80000000UL) {
        p = (int32_t)((b7 * 2) / b4);
    } else {
        p = (int32_t)((b7 / b4) * 2);
    }
    x1 = (p >> 8) * (p >> 8);
    x1 = (x1 * 3038) >> 16;
    x2 = (-7357 * p) >> 16;
    p = p + ((x1 + x2 + 3791) >> 4);
    *pressure = (float)p;

    return 0;
}