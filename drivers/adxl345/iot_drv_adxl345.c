#include "iot_drv_adxl345.h"
#include "iot_logger.h"

#define ADXL345_ADDR         0x53
#define ADXL345_REG_DEVID    0x00
#define ADXL345_REG_POWER_CTL 0x2D
#define ADXL345_REG_DATA_FORMAT 0x31
#define ADXL345_REG_DATAX0   0x32

#define ADXL345_DEVID_VAL    0xE5

int iot_adxl345_init(const iot_i2c_bus_t *bus)
{
    uint8_t id = 0;

    if (bus == NULL) {
        LOG_E("adxl345", "bus null");
        return -1;
    }
    /* DEVID 应读回 0xE5 */
    if (iot_i2c_read_reg(bus, ADXL345_ADDR, ADXL345_REG_DEVID, &id) != 0) {
        LOG_E("adxl345", "read DEVID fail");
        return -1;
    }
    if (id != ADXL345_DEVID_VAL) {
        LOG_E("adxl345", "bad id 0x%02X", id);
        return -1;
    }
    /* 进入测量模式：POWER_CTL 的 MEASURE 位 = 1 */
    iot_i2c_write_reg(bus, ADXL345_ADDR, ADXL345_REG_POWER_CTL, 0x08);
    /* 数据格式：默认 ±2g、右对齐、4 线 SPI */
    iot_i2c_write_reg(bus, ADXL345_ADDR, ADXL345_REG_DATA_FORMAT, 0x00);

    LOG_I("adxl345", "init ok");
    return 0;
}

int iot_adxl345_read(const iot_i2c_bus_t *bus, int16_t acc[3])
{
    uint8_t d[6];

    if (bus == NULL || acc == NULL) {
        return -1;
    }
    if (iot_i2c_read_regs(bus, ADXL345_ADDR, ADXL345_REG_DATAX0, d, 6) != 0) {
        LOG_E("adxl345", "read fail");
        return -1;
    }
    /* 小端解析：低字节在前 */
    acc[0] = (int16_t)(d[0] | (d[1] << 8));
    acc[1] = (int16_t)(d[2] | (d[3] << 8));
    acc[2] = (int16_t)(d[4] | (d[5] << 8));
    return 0;
}