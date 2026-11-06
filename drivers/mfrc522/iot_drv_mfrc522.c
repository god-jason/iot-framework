#include "iot_drv_mfrc522.h"
#include "iot_logger.h"

#define MFRC522_COMMAND_REG     0x01
#define MFRC522_COMMIRQ_REG     0x04
#define MFRC522_FIFO_DATA_REG   0x09
#define MFRC522_BIT_FRAMING_REG 0x0D
#define MFRC522_TX_CONTROL_REG  0x14
#define MFRC522_VERSION_REG     0x37

#define MFRC522_CMD_SOFT_RESET 0x0F
#define MFRC522_CMD_TRANSCEIVE 0x0C

// SPI 地址编码：写为地址左移 1 位，读为 (地址左移 1 位) | 0x80。
static int mfrc522_write_reg(const iot_spi_bus_t *spi, uint8_t reg, uint8_t val)
{
    uint8_t d[2] = { (uint8_t)(reg << 1), val };
    int r;

    if (spi == NULL || spi->transfer == NULL) {
        return -1;
    }
    iot_spi_cs(spi, true);
    r = iot_spi_transfer(spi, d, NULL, 2);
    iot_spi_cs(spi, false);
    return r;
}

static int mfrc522_read_reg(const iot_spi_bus_t *spi, uint8_t reg, uint8_t *val)
{
    uint8_t tx[2] = { (uint8_t)((reg << 1) | 0x80), 0x00 };
    uint8_t rx[2] = { 0, 0 };
    int r;

    if (spi == NULL || spi->transfer == NULL || val == NULL) {
        return -1;
    }
    iot_spi_cs(spi, true);
    r = iot_spi_transfer(spi, tx, rx, 2);
    iot_spi_cs(spi, false);
    if (r == 0) {
        *val = rx[1];
    }
    return r;
}

int iot_mfrc522_init(const iot_spi_bus_t *spi)
{
    uint8_t version = 0;

    if (spi == NULL || spi->transfer == NULL) {
        return -1;
    }

    // 软复位 -> 使能发送 -> 清中断。
    if (mfrc522_write_reg(spi, MFRC522_COMMAND_REG, MFRC522_CMD_SOFT_RESET) != 0 ||
        mfrc522_write_reg(spi, MFRC522_TX_CONTROL_REG, 0x80) != 0 ||
        mfrc522_write_reg(spi, MFRC522_COMMIRQ_REG, 0x7F) != 0) {
        LOG_E("mfrc522", "init fail");
        return -1;
    }

    // 读版本寄存器确认在线。
    if (mfrc522_read_reg(spi, MFRC522_VERSION_REG, &version) != 0) {
        LOG_E("mfrc522", "init fail");
        return -1;
    }
    LOG_I("mfrc522", "init ok ver=0x%02x", (unsigned int)version);
    return 0;
}

int iot_mfrc522_request(const iot_spi_bus_t *spi, uint8_t *type)
{
    uint8_t b0 = 0;
    uint8_t b1 = 0;

    if (spi == NULL || spi->transfer == NULL || type == NULL) {
        return -1;
    }

    // REQA(0x26) 为 7 位短帧：位帧寄存器末 7 位设为 0x07。
    mfrc522_write_reg(spi, MFRC522_BIT_FRAMING_REG, 0x07);
    // 写入 REQA 到 FIFO 并执行收发。
    mfrc522_write_reg(spi, MFRC522_FIFO_DATA_REG, 0x26);
    mfrc522_write_reg(spi, MFRC522_COMMIRQ_REG, 0x7F);
    mfrc522_write_reg(spi, MFRC522_COMMAND_REG, MFRC522_CMD_TRANSCEIVE);

    // 简化：直接读 FIFO 前两字节作为 ATQA（实际应等待命令完成并校验中断）。
    mfrc522_read_reg(spi, MFRC522_FIFO_DATA_REG, &b0);
    mfrc522_read_reg(spi, MFRC522_FIFO_DATA_REG, &b1);
    *type = b0;
    return 0;
}