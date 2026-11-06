#include "iot_drv_ssd1306.h"
#include "iot_logger.h"

#define SSD1306_ADDR 0x3C

// I2C 控制字节：0x00 后跟命令，0x40 后跟显存（GDDRAM）数据。
#define SSD1306_CTRL_CMD  0x00
#define SSD1306_CTRL_DATA 0x40

// 发送若干命令字节：总线先发控制字节 0x00，再依次发送各命令。
static int ssd1306_send_cmd(const iot_i2c_bus_t *bus, const uint8_t *cmd, uint32_t len)
{
    uint8_t buf[32];
    uint32_t i;

    if (len > sizeof(buf) - 1) {
        return -1;
    }
    buf[0] = SSD1306_CTRL_CMD;
    for (i = 0; i < len; i++) {
        buf[1 + i] = cmd[i];
    }
    return bus->write(bus->ctx, SSD1306_ADDR, buf, len + 1);
}

int iot_ssd1306_init(const iot_i2c_bus_t *bus)
{
    // 常见初始化序列：关显示、时钟、复用比、电荷泵、页寻址、方向、开显示等。
    const uint8_t cmd[] = {
        0xAE, 0xD5, 0x80, 0xA8, 0x3F,
        0xD3, 0x00, 0x40,
        0x8D, 0x14,
        0x20, 0x02,
        0xA1, 0xC8,
        0xDA, 0x12,
        0x81, 0x7F,
        0xD9, 0x22, 0xDB, 0x20,
        0xA4, 0xA6,
        0xAF
    };

    if (bus == NULL || bus->write == NULL) {
        return -1;
    }
    if (ssd1306_send_cmd(bus, cmd, sizeof(cmd)) != 0) {
        LOG_E("ssd1306", "init fail");
        return -1;
    }
    LOG_I("ssd1306", "init ok");
    return 0;
}

int iot_ssd1306_fill(const iot_i2c_bus_t *bus, uint8_t pattern)
{
    uint8_t buf[129];
    uint8_t page;
    uint8_t i;

    if (bus == NULL || bus->write == NULL) {
        return -1;
    }

    buf[0] = SSD1306_CTRL_DATA;
    for (i = 0; i < 128; i++) {
        buf[1 + i] = pattern;
    }

    // 页寻址模式共 8 页、每页 128 列：每页先设页与列地址，再写一整页数据。
    for (page = 0; page < 8; page++) {
        const uint8_t addr[3] = { (uint8_t)(0xB0 | page), 0x00, 0x10 };
        if (ssd1306_send_cmd(bus, addr, sizeof(addr)) != 0) {
            return -1;
        }
        if (bus->write(bus->ctx, SSD1306_ADDR, buf, sizeof(buf)) != 0) {
            return -1;
        }
    }
    return 0;
}

int iot_ssd1306_clear(const iot_i2c_bus_t *bus)
{
    return iot_ssd1306_fill(bus, 0x00);
}