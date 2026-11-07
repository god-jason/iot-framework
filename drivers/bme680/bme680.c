#include "bme680.h"
#include "iot_logger.h"

#define BME680_ADDR      0x76
#define BME680_CHIP_ID   0x61

#define BME680_REG_ID    0xD0   /* chip id */
#define BME680_REG_RESET 0xE0   /* 软复位 */
#define BME680_REG_HUM   0x72   /* 湿度过采样 */
#define BME680_REG_CTRL  0x74   /* 温压测量控制 */
#define BME680_REG_DATA  0x1F   /* 压力 3 + 温度 3 + 湿度 2 + 气体 2 字节 */

#define BME680_RESET_VAL 0xB6

int bme680_init(const i2c_bus_t *bus)
{
    uint8_t chip = 0;

    if (bus == NULL) {
        return -1;
    }
    if (i2c_read_reg(bus, BME680_ADDR, BME680_REG_ID, &chip) != 0 ||
        chip != BME680_CHIP_ID) {
        LOG_E("bme680", "chip id fail 0x%02x", chip);
        return -1;
    }
    /* 软复位后等待器件就绪（此处简化跳过延时） */
    if (i2c_write_reg(bus, BME680_ADDR, BME680_REG_RESET, BME680_RESET_VAL) != 0) {
        LOG_E("bme680", "reset fail");
        return -1;
    }
    /* 湿度过采样 x1 */
    if (i2c_write_reg(bus, BME680_ADDR, BME680_REG_HUM, 0x01) != 0) {
        LOG_E("bme680", "hum ctrl fail");
        return -1;
    }
    /* 温度 x1 + 压力 x1 + 强制单次模式 */
    if (i2c_write_reg(bus, BME680_ADDR, BME680_REG_CTRL, 0x25) != 0) {
        LOG_E("bme680", "ctrl fail");
        return -1;
    }
    LOG_I("bme680", "init ok");
    return 0;
}

int bme680_read(const i2c_bus_t *bus, float *temp, float *pressure,
                    float *humidity, float *gas_resistance)
{
    uint8_t d[10];
    int32_t adc_p, adc_t, adc_h;

    if (bus == NULL || temp == NULL || pressure == NULL ||
        humidity == NULL || gas_resistance == NULL) {
        return -1;
    }
    if (i2c_read_regs(bus, BME680_ADDR, BME680_REG_DATA, d, 10) != 0) {
        return -1;
    }
    adc_p = ((int32_t)d[0] << 12) | ((int32_t)d[1] << 4) | ((int32_t)d[2] >> 4);
    adc_t = ((int32_t)d[3] << 12) | ((int32_t)d[4] << 4) | ((int32_t)d[5] >> 4);
    adc_h = ((int32_t)d[6] << 8) | (int32_t)d[7];

    /* 简化：温湿压返回原始 ADC 值，完整补偿需读 0xE1-0xEE 大段系数 */
    *pressure = (float)adc_p;
    *temp = (float)adc_t;
    *humidity = (float)adc_h;

    /* 气体电阻补偿依赖加热控制与多种系数，简化驱动暂不支持，返回 0 */
    *gas_resistance = 0.0f;
    return 0;
}