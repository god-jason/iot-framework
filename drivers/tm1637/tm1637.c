#include "tm1637.h"
#include "iot_logger.h"

#define TM1637_DATA_CMD 0x40  // 自动地址递增写数据
#define TM1637_ADDR_CMD 0xC0  // 起始显示地址 0
#define TM1637_DISP_ON  0x8F  // 显示开且亮度最高

static void tm1637_delay(const gpio_bus_t *gpio)
{
    gpio_udelay(gpio, 5);
}

static void tm1637_start(const gpio_bus_t *gpio, uint8_t clk, uint8_t dio)
{
    gpio_set(gpio, clk, 1);
    gpio_set(gpio, dio, 1);
    tm1637_delay(gpio);
    gpio_set(gpio, dio, 0);
}

static void tm1637_stop(const gpio_bus_t *gpio, uint8_t clk, uint8_t dio)
{
    gpio_set(gpio, clk, 0);
    gpio_set(gpio, dio, 0);
    tm1637_delay(gpio);
    gpio_set(gpio, clk, 1);
    gpio_set(gpio, dio, 1);
}

// 写一字节，低位在前（TM1637 为 LSB 先）。
static void tm1637_write_byte(const gpio_bus_t *gpio, uint8_t clk, uint8_t dio, uint8_t b)
{
    uint8_t i;
    for (i = 0; i < 8; i++) {
        gpio_set(gpio, clk, 0);
        gpio_set(gpio, dio, (b >> i) & 1);
        tm1637_delay(gpio);
        gpio_set(gpio, clk, 1);
        tm1637_delay(gpio);
    }
    // 应答位：总线抽象无方向控制，此处不读 ACK，释放 DIO 并补一个时钟沿。
    gpio_set(gpio, clk, 0);
    gpio_set(gpio, dio, 1);
    tm1637_delay(gpio);
    gpio_set(gpio, clk, 1);
    tm1637_delay(gpio);
    gpio_set(gpio, clk, 0);
}

int tm1637_init(const gpio_bus_t *gpio, uint8_t clk, uint8_t dio)
{
    if (gpio == NULL || gpio->set_level == NULL) {
        return -1;
    }
    gpio_set(gpio, clk, 1);
    gpio_set(gpio, dio, 1);

    tm1637_start(gpio, clk, dio);
    tm1637_write_byte(gpio, clk, dio, TM1637_DISP_ON);
    tm1637_stop(gpio, clk, dio);

    LOG_I("tm1637", "init ok");
    return 0;
}

int tm1637_display(const gpio_bus_t *gpio, uint8_t clk, uint8_t dio,
                       const uint8_t *seg, uint8_t n)
{
    uint8_t i;

    if (gpio == NULL || gpio->set_level == NULL || (seg == NULL && n > 0)) {
        return -1;
    }

    // 起始 -> 数据命令 0x40 -> 停止；再起始 -> 地址 0xC0 -> 依次写 n 个段码 -> 停止。
    tm1637_start(gpio, clk, dio);
    tm1637_write_byte(gpio, clk, dio, TM1637_DATA_CMD);
    tm1637_stop(gpio, clk, dio);

    tm1637_start(gpio, clk, dio);
    tm1637_write_byte(gpio, clk, dio, TM1637_ADDR_CMD);
    for (i = 0; i < n && i < 6; i++) {
        tm1637_write_byte(gpio, clk, dio, seg[i]);
    }
    tm1637_stop(gpio, clk, dio);

    // 最后发送显示开命令，点亮屏幕。
    tm1637_start(gpio, clk, dio);
    tm1637_write_byte(gpio, clk, dio, TM1637_DISP_ON);
    tm1637_stop(gpio, clk, dio);
    return 0;
}