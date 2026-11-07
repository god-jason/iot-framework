#include "bme280.h"
#include "iot_logger.h"

#define BME280_ADDR      0x76
#define BME280_CHIP_ID   0x60

#define BME280_REG_ID    0xD0   /* chip id */
#define BME280_REG_CTRL  0xF4   /* 温压测量控制 */
#define BME280_REG_HUM   0xF2   /* 湿度测量控制 */
#define BME280_REG_CAL   0x88   /* 温压校准系数起始，24 字节 */
#define BME280_REG_HCAL  0xA1   /* 湿度校准系数段（0xA1 及 0xE1-0xE7） */
#define BME280_REG_DATA  0xF7   /* 压力 3 字节 + 温度 3 字节 + 湿度 2 字节 */

/* 温压校准系数（与 BMP280 相同布局） */
static uint16_t bme280_t1, bme280_p1;
static int16_t  bme280_t2, bme280_t3;
static int16_t  bme280_p2, bme280_p3, bme280_p4, bme280_p5;
static int16_t  bme280_p6, bme280_p7, bme280_p8, bme280_p9;

/* 湿度校准系数 */
static uint8_t  bme280_h1, bme280_h3;
static int16_t  bme280_h2, bme280_h4, bme280_h5;
static int8_t   bme280_h6;

static int      bme280_calib = 0;
static int32_t  bme280_t_fine = 0;

int bme280_init(const i2c_bus_t *bus)
{
    uint8_t cal[24];
    uint8_t h[8];
    uint8_t chip = 0;

    if (bus == NULL) {
        return -1;
    }
    if (i2c_read_reg(bus, BME280_ADDR, BME280_REG_ID, &chip) != 0 ||
        chip != BME280_CHIP_ID) {
        LOG_E("bme280", "chip id fail 0x%02x", chip);
        return -1;
    }
    /* 温度 x1 + 压力 x1 + normal 模式 */
    if (i2c_write_reg(bus, BME280_ADDR, BME280_REG_CTRL, 0x27) != 0) {
        LOG_E("bme280", "ctrl write fail");
        return -1;
    }
    /* 湿度过采样 x1 */
    if (i2c_write_reg(bus, BME280_ADDR, BME280_REG_HUM, 0x01) != 0) {
        LOG_E("bme280", "hum ctrl write fail");
        return -1;
    }

    if (i2c_read_regs(bus, BME280_ADDR, BME280_REG_CAL, cal, 24) != 0) {
        LOG_E("bme280", "cal read fail");
        return -1;
    }
    bme280_t1 = (uint16_t)(cal[0] | (cal[1] << 8));
    bme280_t2 = (int16_t)(cal[2] | (cal[3] << 8));
    bme280_t3 = (int16_t)(cal[4] | (cal[5] << 8));
    bme280_p1 = (uint16_t)(cal[6] | (cal[7] << 8));
    bme280_p2 = (int16_t)(cal[8] | (cal[9] << 8));
    bme280_p3 = (int16_t)(cal[10] | (cal[11] << 8));
    bme280_p4 = (int16_t)(cal[12] | (cal[13] << 8));
    bme280_p5 = (int16_t)(cal[14] | (cal[15] << 8));
    bme280_p6 = (int16_t)(cal[16] | (cal[17] << 8));
    bme280_p7 = (int16_t)(cal[18] | (cal[19] << 8));
    bme280_p8 = (int16_t)(cal[20] | (cal[21] << 8));
    bme280_p9 = (int16_t)(cal[22] | (cal[23] << 8));

    /* 湿度校准：0xA1(1B) 与 0xE1-0xE7(7B)，分开读取 */
    if (i2c_read_reg(bus, BME280_ADDR, BME280_REG_HCAL, &h[0]) != 0) {
        LOG_E("bme280", "hum cal h1 fail");
        return -1;
    }
    if (i2c_read_regs(bus, BME280_ADDR, 0xE1, &h[1], 7) != 0) {
        LOG_E("bme280", "hum cal h2-h6 fail");
        return -1;
    }
    bme280_h1 = h[0];
    bme280_h2 = (int16_t)(h[1] | (h[2] << 8));
    bme280_h3 = h[3];
    bme280_h4 = (int16_t)((h[4] << 4) | (h[5] & 0x0F));
    bme280_h5 = (int16_t)((h[6] << 4) | (h[5] >> 4));
    bme280_h6 = (int8_t)h[7];

    bme280_calib = 1;
    LOG_I("bme280", "init ok");
    return 0;
}

static int32_t bme280_compensate_temp(int32_t adc_t)
{
    int32_t v1, v2;

    v1 = ((((adc_t >> 3) - ((int32_t)bme280_t1 << 1))) * (int32_t)bme280_t2) >> 11;
    v2 = (((((adc_t >> 4) - (int32_t)bme280_t1) *
            ((adc_t >> 4) - (int32_t)bme280_t1)) >> 12) * (int32_t)bme280_t3) >> 14;
    bme280_t_fine = v1 + v2;
    return (bme280_t_fine * 5 + 128) >> 8;
}

static uint32_t bme280_compensate_press(int32_t adc_p)
{
    int64_t v1, v2, p;

    v1 = (int64_t)bme280_t_fine - 128000;
    v2 = v1 * v1 * (int64_t)bme280_p6;
    v2 = v2 + ((v1 * (int64_t)bme280_p5) << 17);
    v2 = v2 + (((int64_t)bme280_p4) << 35);
    v1 = ((v1 * v1 * (int64_t)bme280_p3) >> 8) + ((v1 * (int64_t)bme280_p2) << 12);
    v1 = (((((int64_t)1) << 47) + v1) * (int64_t)bme280_p1) >> 33;
    if (v1 == 0) {
        return 0;
    }
    p = 1048576 - adc_p;
    p = (((p << 31) - v2) * 3125) / v1;
    v1 = (((int64_t)bme280_p9) * (p >> 13) * (p >> 13)) >> 25;
    v2 = (((int64_t)bme280_p8) * p) >> 19;
    p = ((p + v1 + v2) >> 8) + (((int64_t)bme280_p7) << 4);
    return (uint32_t)p;
}

/* 湿度补偿，返回 %RH 的 1024 倍 */
static uint32_t bme280_compensate_hum(int32_t adc_h)
{
    int32_t v;

    v = bme280_t_fine - (int32_t)76800;
    v = (((((int32_t)adc_h << 14) - ((int32_t)bme280_h4 << 20) -
           ((int32_t)bme280_h5 * v)) + (int32_t)16384) >> 15) *
        (((((((v * (int32_t)bme280_h6) >> 10) *
             (((v * (int32_t)bme280_h3) >> 11) + (int32_t)32768)) >> 10) +
           (int32_t)2097152) * (int32_t)bme280_h2 + 8192) >> 14);
    v = v - (((((v >> 15) * (v >> 15)) >> 7) * (int32_t)bme280_h1) >> 4);
    if (v < 0) {
        v = 0;
    }
    if (v > 419430400) {
        v = 419430400;
    }
    return (uint32_t)(v >> 12);
}

int bme280_read(const i2c_bus_t *bus, float *temp, float *pressure, float *humidity)
{
    uint8_t d[8];
    int32_t adc_p, adc_t, adc_h;

    if (bus == NULL || temp == NULL || pressure == NULL || humidity == NULL ||
        bme280_calib == 0) {
        return -1;
    }
    if (i2c_read_regs(bus, BME280_ADDR, BME280_REG_DATA, d, 8) != 0) {
        return -1;
    }
    adc_p = ((int32_t)d[0] << 12) | ((int32_t)d[1] << 4) | ((int32_t)d[2] >> 4);
    adc_t = ((int32_t)d[3] << 12) | ((int32_t)d[4] << 4) | ((int32_t)d[5] >> 4);
    adc_h = ((int32_t)d[6] << 8) | (int32_t)d[7];

    *temp = (float)bme280_compensate_temp(adc_t) / 100.0f;
    *pressure = (float)bme280_compensate_press(adc_p);
    *humidity = (float)bme280_compensate_hum(adc_h) / 1024.0f;
    return 0;
}