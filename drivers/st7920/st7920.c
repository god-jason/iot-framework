#include "st7920.h"
#include "iot_logger.h"

// 引脚号在 init 时记录到静态变量，后续 write 复用（单实例使用）。
static uint8_t s_rs, s_rw, s_en, s_d4, s_d5, s_d6, s_d7;

static void st7920_delay(const gpio_bus_t *gpio)
{
    gpio_udelay(gpio, 50);
}

static void st7920_set_data(const gpio_bus_t *gpio, uint8_t nib)
{
    gpio_set(gpio, s_d4, (nib >> 0) & 1);
    gpio_set(gpio, s_d5, (nib >> 1) & 1);
    gpio_set(gpio, s_d6, (nib >> 2) & 1);
    gpio_set(gpio, s_d7, (nib >> 3) & 1);
}

static void st7920_pulse(const gpio_bus_t *gpio)
{
    gpio_set(gpio, s_en, 1);
    st7920_delay(gpio);
    gpio_set(gpio, s_en, 0);
}

static void st7920_write_nibble(const gpio_bus_t *gpio, uint8_t rs, uint8_t nib)
{
    gpio_set(gpio, s_rs, rs);
    gpio_set(gpio, s_rw, 0);
    st7920_set_data(gpio, nib);
    st7920_pulse(gpio);
}

static void st7920_write_byte(const gpio_bus_t *gpio, uint8_t rs, uint8_t b)
{
    st7920_write_nibble(gpio, rs, (b >> 4) & 0x0F);
    st7920_write_nibble(gpio, rs, b & 0x0F);
}

static void st7920_cmd(const gpio_bus_t *gpio, uint8_t cmd)
{
    st7920_write_byte(gpio, 0, cmd);
}

static void st7920_data(const gpio_bus_t *gpio, uint8_t d)
{
    st7920_write_byte(gpio, 1, d);
}

int st7920_init(const gpio_bus_t *gpio, uint8_t rs, uint8_t rw, uint8_t en,
                    uint8_t d4, uint8_t d5, uint8_t d6, uint8_t d7)
{
    if (gpio == NULL || gpio->set_level == NULL || gpio->udelay == NULL) {
        return -1;
    }
    s_rs = rs; s_rw = rw; s_en = en;
    s_d4 = d4; s_d5 = d5; s_d6 = d6; s_d7 = d7;

    // 数据手册标准流程：先强制进入 8 位模式，再切换为 4 位模式。
    st7920_write_nibble(gpio, 0, 0x03);
    gpio_udelay(gpio, 4500);
    st7920_write_nibble(gpio, 0, 0x03);
    gpio_udelay(gpio, 150);
    st7920_write_nibble(gpio, 0, 0x03);
    st7920_write_nibble(gpio, 0, 0x02);

    st7920_cmd(gpio, 0x28);  // 功能设置：4 位、2 行、5x8
    st7920_cmd(gpio, 0x08);  // 显示关
    st7920_cmd(gpio, 0x01);  // 清屏
    gpio_udelay(gpio, 2000);
    st7920_cmd(gpio, 0x06);  // 入口模式：光标右移
    st7920_cmd(gpio, 0x0C);  // 显示开、光标关
    LOG_I("st7920", "init ok");
    return 0;
}

int st7920_write(const gpio_bus_t *gpio, const char *str)
{
    if (gpio == NULL || gpio->set_level == NULL || str == NULL) {
        return -1;
    }
    while (*str) {
        st7920_data(gpio, (uint8_t)*str++);
    }
    return 0;
}