#include "max7219.h"
#include "iot_logger.h"

// 向指定寄存器地址写一字节数据：CS 拉低 -> 发地址与数据 -> CS 拉高。
int max7219_write(const spi_bus_t *spi, uint8_t addr, uint8_t data)
{
    uint8_t d[2] = { addr, data };
    int r;

    if (spi == NULL || spi->transfer == NULL) {
        return -1;
    }
    spi_cs(spi, true);
    r = spi_transfer(spi, d, NULL, 2);
    spi_cs(spi, false);
    return r;
}

int max7219_init(const spi_bus_t *spi)
{
    if (spi == NULL || spi->transfer == NULL) {
        return -1;
    }

    // 关解码 0x09、亮度 0x0A、扫描限制 0x0B、关测试 0x0F、正常工作 0x0C。
    if (max7219_write(spi, 0x09, 0x00) != 0 ||
        max7219_write(spi, 0x0A, 0x03) != 0 ||
        max7219_write(spi, 0x0B, 0x07) != 0 ||
        max7219_write(spi, 0x0F, 0x00) != 0 ||
        max7219_write(spi, 0x0C, 0x01) != 0) {
        LOG_E("max7219", "init fail");
        return -1;
    }
    LOG_I("max7219", "init ok");
    return 0;
}

int max7219_clear(const spi_bus_t *spi)
{
    uint8_t i;

    if (spi == NULL || spi->transfer == NULL) {
        return -1;
    }
    // 8 位数字寄存器 0x01..0x08 全部清零。
    for (i = 0x01; i <= 0x08; i++) {
        if (max7219_write(spi, i, 0x00) != 0) {
            return -1;
        }
    }
    return 0;
}