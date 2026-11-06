#include "iot_drv_max6675.h"
#include "iot_logger.h"

int iot_max6675_init(const iot_spi_bus_t *spi)
{
    if (spi == NULL) {
        return -1;
    }
    LOG_I("max6675", "init ok");
    return 0;
}

int iot_max6675_read(const iot_spi_bus_t *spi, float *temp)
{
    uint8_t tx[2] = { 0x00, 0x00 };
    uint8_t rx[2] = { 0x00, 0x00 };
    uint16_t raw;
    int ret;

    if (spi == NULL || temp == NULL) {
        return -1;
    }

    /* 拉低片选，读取 2 字节 */
    iot_spi_cs(spi, true);
    ret = iot_spi_transfer(spi, tx, rx, 2);
    iot_spi_cs(spi, false);
    if (ret != 0) {
        return -1;
    }

    raw = (uint16_t)((rx[0] << 8) | rx[1]);

    /* D2=1 表示热电偶开路 */
    if (raw & 0x04) {
        LOG_E("max6675", "thermocouple open");
        return -2;
    }

    /* 高 12 位为温度，0.25℃ / bit */
    *temp = (float)(raw >> 3) * 0.25f;
    return 0;
}