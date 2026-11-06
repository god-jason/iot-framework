#include "iot_drv_max31865.h"
#include "iot_logger.h"

#define MAX31865_REG_CONFIG  0x00    /* 配置寄存器 */
#define MAX31865_REG_RTD_MSB 0x01    /* RTD 高位寄存器 */
#define MAX31865_REG_RTD_LSB 0x02    /* RTD 低位寄存器 */
#define MAX31865_CONF_VBIAS  0x80    /* bit7：使能偏置电压 */

int iot_max31865_init(const iot_spi_bus_t *spi)
{
    uint8_t tx[2] = { MAX31865_REG_CONFIG, MAX31865_CONF_VBIAS };
    uint8_t rx[2] = { 0, 0 };
    int ret;

    if (spi == NULL) {
        return -1;
    }

    /* 写配置寄存器 0x00（最高位 0 表示写），使能 VBIAS */
    iot_spi_cs(spi, true);
    ret = iot_spi_transfer(spi, tx, rx, 2);
    iot_spi_cs(spi, false);
    if (ret != 0) {
        LOG_E("max31865", "init fail");
        return -1;
    }
    LOG_I("max31865", "init ok");
    return 0;
}

int iot_max31865_read(const iot_spi_bus_t *spi, float *temp)
{
    uint8_t tx[2];
    uint8_t rx[2];
    uint16_t raw;
    int ret;

    if (spi == NULL || temp == NULL) {
        return -1;
    }

    /* 读 RTD 高位：地址最高位置 1 表示读 */
    iot_spi_cs(spi, true);
    tx[0] = 0x80 | MAX31865_REG_RTD_MSB;
    tx[1] = 0x00;
    ret = iot_spi_transfer(spi, tx, rx, 2);
    if (ret != 0) {
        iot_spi_cs(spi, false);
        return -1;
    }
    raw = (uint16_t)rx[1] << 8;

    /* 读 RTD 低位 */
    tx[0] = 0x80 | MAX31865_REG_RTD_LSB;
    tx[1] = 0x00;
    ret = iot_spi_transfer(spi, tx, rx, 2);
    iot_spi_cs(spi, false);
    if (ret != 0) {
        return -1;
    }
    raw |= rx[1];

    /* bit0 为故障标志，右移 1 位得到 15 位 ADC 值 */
    raw >>= 1;

    /* 线性近似：temp≈raw/32-256（精确换算需铂电阻分度表） */
    *temp = (float)raw / 32.0f - 256.0f;
    return 0;
}