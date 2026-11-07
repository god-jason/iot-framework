#include "bmp280.h"
#include "iot_logger.h"

#define BMP280_ADDR      0x76
#define BMP280_CHIP_ID   0x58

#define BMP280_REG_ID    0xD0   /* chip id */
#define BMP280_REG_CTRL  0xF4   /* 测量控制：温压过采样与模式 */
#define BMP280_REG_CAL   0x88   /* 温度压力校准系数起始，24 字节 */
#define BMP280_REG_DATA  0xF7   /* 压力 3 字节 + 温度 3 字节（大端） */

/* 校准系数 */
static uint16_t bmp280_t1, bmp280_p1;
static int16_t  bmp280_t2, bmp280_t3;
static int16_t  bmp280_p2, bmp280_p3, bmp280_p4, bmp280_p5;
static int16_t  bmp280_p6, bmp280_p7, bmp280_p8, bmp280_p9;
static int      bmp280_calib = 0;

/* t_fine：全局温度补偿中间量，压力补偿复用 */
static int32_t  bmp280_t_fine = 0;

int bmp280_init(const i2c_bus_t *bus)
{
    uint8_t cal[24];
    uint8_t chip = 0;

    if (bus == NULL) {
        return -1;
    }
    if (i2c_read_reg(bus, BMP280_ADDR, BMP280_REG_ID, &chip) != 0 ||
        chip != BMP280_CHIP_ID) {
        LOG_E("bmp280", "chip id fail 0x%02x", chip);
        return -1;
    }
    /* ctrl_meas = 0x27：温度过采样 x1 + 压力过采样 x1 + normal 模式 */
    if (i2c_write_reg(bus, BMP280_ADDR, BMP280_REG_CTRL, 0x27) != 0) {
        LOG_E("bmp280", "ctrl write fail");
        return -1;
    }
    if (i2c_read_regs(bus, BMP280_ADDR, BMP280_REG_CAL, cal, 24) != 0) {
        LOG_E("bmp280", "cal read fail");
        return -1;
    }
    bmp280_t1 = (uint16_t)(cal[0] | (cal[1] << 8));
    bmp280_t2 = (int16_t)(cal[2] | (cal[3] << 8));
    bmp280_t3 = (int16_t)(cal[4] | (cal[5] << 8));
    bmp280_p1 = (uint16_t)(cal[6] | (cal[7] << 8));
    bmp280_p2 = (int16_t)(cal[8] | (cal[9] << 8));
    bmp280_p3 = (int16_t)(cal[10] | (cal[11] << 8));
    bmp280_p4 = (int16_t)(cal[12] | (cal[13] << 8));
    bmp280_p5 = (int16_t)(cal[14] | (cal[15] << 8));
    bmp280_p6 = (int16_t)(cal[16] | (cal[17] << 8));
    bmp280_p7 = (int16_t)(cal[18] | (cal[19] << 8));
    bmp280_p8 = (int16_t)(cal[20] | (cal[21] << 8));
    bmp280_p9 = (int16_t)(cal[22] | (cal[23] << 8));

    bmp280_calib = 1;
    LOG_I("bmp280", "init ok");
    return 0;
}

/* 温度补偿，返回 0.01℃ 单位，并更新 t_fine */
static int32_t bmp280_compensate_temp(int32_t adc_t)
{
    int32_t v1, v2;

    v1 = ((((adc_t >> 3) - ((int32_t)bmp280_t1 << 1))) * (int32_t)bmp280_t2) >> 11;
    v2 = (((((adc_t >> 4) - (int32_t)bmp280_t1) *
            ((adc_t >> 4) - (int32_t)bmp280_t1)) >> 12) * (int32_t)bmp280_t3) >> 14;
    bmp280_t_fine = v1 + v2;
    return (bmp280_t_fine * 5 + 128) >> 8;
}

/* 压力补偿，返回 Pa（须先调用温度补偿） */
static uint32_t bmp280_compensate_press(int32_t adc_p)
{
    int64_t v1, v2, p;

    v1 = (int64_t)bmp280_t_fine - 128000;
    v2 = v1 * v1 * (int64_t)bmp280_p6;
    v2 = v2 + ((v1 * (int64_t)bmp280_p5) << 17);
    v2 = v2 + (((int64_t)bmp280_p4) << 35);
    v1 = ((v1 * v1 * (int64_t)bmp280_p3) >> 8) + ((v1 * (int64_t)bmp280_p2) << 12);
    v1 = (((((int64_t)1) << 47) + v1) * (int64_t)bmp280_p1) >> 33;
    if (v1 == 0) {
        return 0;
    }
    p = 1048576 - adc_p;
    p = (((p << 31) - v2) * 3125) / v1;
    v1 = (((int64_t)bmp280_p9) * (p >> 13) * (p >> 13)) >> 25;
    v2 = (((int64_t)bmp280_p8) * p) >> 19;
    p = ((p + v1 + v2) >> 8) + (((int64_t)bmp280_p7) << 4);
    return (uint32_t)p;
}

int bmp280_read(const i2c_bus_t *bus, float *temp, float *pressure)
{
    uint8_t d[6];
    int32_t adc_p, adc_t, t;
    uint32_t p;

    if (bus == NULL || temp == NULL || pressure == NULL || bmp280_calib == 0) {
        return -1;
    }
    if (i2c_read_regs(bus, BMP280_ADDR, BMP280_REG_DATA, d, 6) != 0) {
        return -1;
    }
    adc_p = ((int32_t)d[0] << 12) | ((int32_t)d[1] << 4) | ((int32_t)d[2] >> 4);
    adc_t = ((int32_t)d[3] << 12) | ((int32_t)d[4] << 4) | ((int32_t)d[5] >> 4);

    t = bmp280_compensate_temp(adc_t);
    p = bmp280_compensate_press(adc_p);

    *temp = (float)t / 100.0f;
    *pressure = (float)p;
    return 0;
}