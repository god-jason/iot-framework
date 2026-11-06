#include "iot_drv_hx711.h"
#include "iot_logger.h"

#define HX711_MAX_WAIT_US   100000  /* 等待 DOUT 拉低的最长时间(µs) */
#define HX711_PULSE_US      1       /* 脉冲高低电平保持时间(µs) */

int iot_hx711_init(const iot_gpio_bus_t *gpio, uint8_t sck, uint8_t dout)
{
    (void)dout;
    if (gpio == NULL) {
        return -1;
    }
    iot_gpio_set(gpio, sck, 0);
    LOG_I("hx711", "init ok");
    return 0;
}

int iot_hx711_read(const iot_gpio_bus_t *gpio, uint8_t sck, uint8_t dout, int32_t *value)
{
    uint32_t raw = 0;
    uint32_t timeout = 0;
    uint32_t i;

    if (gpio == NULL || value == NULL) {
        return -1;
    }

    /* 等待 DOUT 拉低（数据就绪） */
    while (iot_gpio_get(gpio, dout) != 0) {
        if (++timeout > HX711_MAX_WAIT_US) {
            LOG_E("hx711", "dout timeout");
            return -1;
        }
        iot_gpio_udelay(gpio, 1);
    }

    /* 前 24 个脉冲读出 24 位大端数据 */
    for (i = 0; i < 24; i++) {
        iot_gpio_set(gpio, sck, 1);
        iot_gpio_udelay(gpio, HX711_PULSE_US);
        raw <<= 1;
        if (iot_gpio_get(gpio, dout) != 0) {
            raw |= 1;
        }
        iot_gpio_set(gpio, sck, 0);
        iot_gpio_udelay(gpio, HX711_PULSE_US);
    }

    /* 第 25 个脉冲：结束本次并选择下一周期为通道 A / 增益 128 */
    iot_gpio_set(gpio, sck, 1);
    iot_gpio_udelay(gpio, HX711_PULSE_US);
    iot_gpio_set(gpio, sck, 0);

    /* 24 位补码符号扩展为有符号 32 位 */
    if (raw & 0x800000UL) {
        raw |= 0xFF000000UL;
    }
    *value = (int32_t)raw;
    return 0;
}