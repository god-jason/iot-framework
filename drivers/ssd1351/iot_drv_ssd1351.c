#include "iot_drv_ssd1351.h"
#include "iot_logger.h"

// 无 DC 线连接时：命令以 0x80 作为字头、命令字节紧随其后，数据则直接发送。
// 若硬件提供 DC 线，可改为拉高拉低 DC 来区分命令与数据，本实现不依赖 DC。
#define SSD1351_CMD_HDR 0x80

// 发送一条命令（可附最多 6 字节参数），命令帧为 0x80 + 命令 + 参数。
static int ssd1351_cmd(const iot_spi_bus_t *spi, uint8_t cmd, const uint8_t *arg, uint32_t arg_len)
{
    uint8_t buf[8];
    uint32_t i;

    if (spi == NULL || spi->transfer == NULL) {
        return -1;
    }
    if (arg_len > sizeof(buf) - 2) {
        return -1;
    }
    buf[0] = SSD1351_CMD_HDR;
    buf[1] = cmd;
    for (i = 0; i < arg_len; i++) {
        buf[2 + i] = arg[i];
    }
    return iot_spi_transfer(spi, buf, NULL, arg_len + 2);
}

int iot_ssd1351_init(const iot_spi_bus_t *spi)
{
    if (spi == NULL || spi->transfer == NULL) {
        return -1;
    }

    // 关闭显示并配置基本几何、时钟与显示参数，最后打开显示。
    if (ssd1351_cmd(spi, 0xAE, NULL, 0) != 0 ||
        ssd1351_cmd(spi, 0xA0, (const uint8_t[]){ 0xB4 }, 1) != 0 ||
        ssd1351_cmd(spi, 0xA1, (const uint8_t[]){ 0x00 }, 1) != 0 ||
        ssd1351_cmd(spi, 0xA2, (const uint8_t[]){ 0x00 }, 1) != 0 ||
        ssd1351_cmd(spi, 0xA4, NULL, 0) != 0 ||
        ssd1351_cmd(spi, 0x15, (const uint8_t[]){ 0x00, 0x7F }, 2) != 0 ||
        ssd1351_cmd(spi, 0x75, (const uint8_t[]){ 0x00, 0x7F }, 2) != 0 ||
        ssd1351_cmd(spi, 0xB1, (const uint8_t[]){ 0x74 }, 1) != 0 ||
        ssd1351_cmd(spi, 0xB2, (const uint8_t[]){ 0x00 }, 1) != 0 ||
        ssd1351_cmd(spi, 0xB3, (const uint8_t[]){ 0xF0 }, 1) != 0 ||
        ssd1351_cmd(spi, 0xB6, (const uint8_t[]){ 0x02 }, 1) != 0 ||
        ssd1351_cmd(spi, 0xBE, (const uint8_t[]){ 0x05 }, 1) != 0 ||
        ssd1351_cmd(spi, 0xC1, (const uint8_t[]){ 0xC8, 0x80, 0xC8 }, 3) != 0 ||
        ssd1351_cmd(spi, 0xC7, (const uint8_t[]){ 0x0F }, 1) != 0 ||
        ssd1351_cmd(spi, 0xB9, NULL, 0) != 0 ||
        ssd1351_cmd(spi, 0xAF, NULL, 0) != 0) {
        LOG_E("ssd1351", "init fail");
        return -1;
    }
    LOG_I("ssd1351", "init ok");
    return 0;
}

int iot_ssd1351_fill(const iot_spi_bus_t *spi, uint16_t color)
{
    uint8_t chunk[256];
    uint16_t row;
    uint16_t i;

    if (spi == NULL || spi->transfer == NULL) {
        return -1;
    }

    // 先发送写显存命令 0x5C，再连续刷 128x128 个像素（每像素 2 字节 RGB565）。
    if (ssd1351_cmd(spi, 0x5C, NULL, 0) != 0) {
        return -1;
    }

    for (i = 0; i < 128; i++) {
        chunk[2 * i] = (uint8_t)(color >> 8);
        chunk[2 * i + 1] = (uint8_t)(color & 0xFF);
    }

    // 行长 256 字节，共 128 行，逐行写出以控制单次传输长度。
    for (row = 0; row < 128; row++) {
        if (iot_spi_transfer(spi, chunk, NULL, sizeof(chunk)) != 0) {
            return -1;
        }
    }
    return 0;
}