#include "iot_sensor_bmi270.h"
#include "iot_logger.h"

/*
 * BMI270 需上电后加载配置固件并切换至应用模式，否则数据寄存器无效。
 * 本驱动仅做简化处理：读取芯片 ID、直接读数据寄存器，
 * 数据 0x0C 起 12 字节为 acc[3]+gyro[3]（小端）。
 */
#define BMI270_ADDR    0x68
#define BMI270_CHIP_ID 0x00
#define BMI270_DATA    0x0C

#define BMI270_CHIP_ID_VAL 0x24

int iot_bmi270_init(const iot_i2c_bus_t *bus)
{
    uint8_t id = 0;

    if (bus == NULL) {
        LOG_E("bmi270", "bus null");
        return -1;
    }
    if (iot_i2c_read_reg(bus, BMI270_ADDR, BMI270_CHIP_ID, &id) != 0) {
        LOG_E("bmi270", "read chip id fail");
        return -1;
    }
    LOG_I("bmi270", "chip id 0x%02X, need config load", id);
    return 0;
}

int iot_bmi270_read(const iot_i2c_bus_t *bus, int16_t acc[3], int16_t gyro[3])
{
    uint8_t d[12];

    if (bus == NULL || acc == NULL || gyro == NULL) {
        return -1;
    }
    if (iot_i2c_read_regs(bus, BMI270_ADDR, BMI270_DATA, d, 12) != 0) {
        LOG_E("bmi270", "read fail");
        return -1;
    }
    /* 小端解析：0x0C 起 acc(6B) + gyro(6B) */
    acc[0] = (int16_t)(d[0] | (d[1] << 8));
    acc[1] = (int16_t)(d[2] | (d[3] << 8));
    acc[2] = (int16_t)(d[4] | (d[5] << 8));
    gyro[0] = (int16_t)(d[6] | (d[7] << 8));
    gyro[1] = (int16_t)(d[8] | (d[9] << 8));
    gyro[2] = (int16_t)(d[10] | (d[11] << 8));
    return 0;
}