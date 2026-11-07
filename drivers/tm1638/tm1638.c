#include "tm1638.h"
#include "iot_logger.h"

// TM1638 数据高位在前（MSB 先），在 CLK 上升沿被采样。
static void tm1638_write_byte(const gpio_bus_t *gpio, uint8_t clk, uint8_t dio, uint8_t b)
{
    uint8_t i;
    for (i = 0; i < 8; i++) {
        gpio_set(gpio, clk, 0);
        gpio_set(gpio, dio, (b >> (7 - i)) & 1);
        gpio_udelay(gpio, 1);
        gpio_set(gpio, clk, 1);
        gpio_udelay(gpio, 1);
    }
}

// STB 拉低 -> 发命令 -> 发数据 -> STB 拉高。
int tm1638_write(const gpio_bus_t *gpio, uint8_t stb, uint8_t clk, uint8_t dio,
                     uint8_t cmd, const uint8_t *data, uint8_t len)
{
    uint8_t i;

    if (gpio == NULL || gpio->set_level == NULL || (data == NULL && len > 0)) {
        return -1;
    }

    gpio_set(gpio, stb, 0);
    tm1638_write_byte(gpio, clk, dio, cmd);
    for (i = 0; i < len; i++) {
        tm1638_write_byte(gpio, clk, dio, data[i]);
    }
    gpio_set(gpio, stb, 1);
    return 0;
}

int tm1638_init(const gpio_bus_t *gpio, uint8_t stb, uint8_t clk, uint8_t dio)
{
    if (gpio == NULL || gpio->set_level == NULL) {
        return -1;
    }

    gpio_set(gpio, stb, 1);
    gpio_set(gpio, clk, 1);
    gpio_set(gpio, dio, 1);

    // 显示开、最高亮度：0x88 | 亮度(0..7)。
    if (tm1638_write(gpio, stb, clk, dio, 0x8F, NULL, 0) != 0) {
        LOG_E("tm1638", "init fail");
        return -1;
    }
    LOG_I("tm1638", "init ok");
    return 0;
}