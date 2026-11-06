#include "iot_sensor_spl06_001.h"
#include "iot_logger.h"

#define SPL06_ADDR      0x76
#define SPL06_CHIP_ID   0x10

#define SPL06_REG_PSR   0x00   /* 压力 24bit（二进制补码，3 字节） */
#define SPL06_REG_TMP   0x03   /* 温度 24bit（二进制补码，3 字节） */
#define SPL06_REG_ID    0x0D   /* 产品 / 版本 id */
#define SPL06_REG_CFG   0x06   /* 压力过采样 */
#define SPL06_REG_TST   0x07   /* 温度过采样 */
#define SPL06_REG_MCFG  0x08   /* 测量控制 */

int iot_spl06_init(const iot_i2c_bus_t *bus)
{
    uint8_t id = 0;

    if (bus == NULL) {
        return -1;
    }
    if (iot_i2c_read_reg(bus, SPL06_ADDR, SPL06_REG_ID, &id) != 0 ||
        id != SPL06_CHIP_ID) {
        LOG_E("spl06", "chip id fail 0x%02x", id);
        return -1;
    }
    /* 压力过采样 x8、温度过采样 x8 */
    if (iot_i2c_write_reg(bus, SPL06_ADDR, SPL06_REG_CFG, 0x02) != 0 ||
        iot_i2c_write_reg(bus, SPL06_ADDR, SPL06_REG_TST, 0x02) != 0) {
        LOG_E("spl06", "osr write fail");
        return -1;
    }
    /* 0x07：连续模式，压力 + 温度同时测量 */
    if (iot_i2c_write_reg(bus, SPL06_ADDR, SPL06_REG_MCFG, 0x07) != 0) {
        LOG_E("spl06", "measure ctrl fail");
        return -1;
    }
    LOG_I("spl06", "init ok");
    return 0;
}

int iot_spl06_read(const iot_i2c_bus_t *bus, float *temp, float *pressure)
{
    uint8_t d[6];
    int32_t raw_p, raw_t;

    if (bus == NULL || temp == NULL || pressure == NULL) {
        return -1;
    }
    if (iot_i2c_read_regs(bus, SPL06_ADDR, SPL06_REG_PSR, d, 6) != 0) {
        return -1;
    }
    /* 24 位二进制补码，最高字节搬符号位做符号扩展 */
    raw_p = ((int32_t)d[0] << 16) | ((int32_t)d[1] << 8) | (int32_t)d[2];
    if (raw_p & 0x00800000) {
        raw_p |= (int32_t)0xFF000000;
    }
    raw_t = ((int32_t)d[3] << 16) | ((int32_t)d[4] << 8) | (int32_t)d[5];
    if (raw_t & 0x00800000) {
        raw_t |= (int32_t)0xFF000000;
    }

    /* 简化：补偿需读取 0x0C 起 18 字节系数（c00/c10 等），此处先返回原始值 */
    *pressure = (float)raw_p;
    *temp = (float)raw_t;
    return 0;
}