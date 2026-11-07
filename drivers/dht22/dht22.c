#include "dht22.h"
#include "iot_logger.h"

/* DHT22 时序参数（单位 us），单总线协议与 DHT11 一致、分辨率更高 */
#define DHT_START_LOW_US   18000  /* 主机拉低 18ms 唤醒 */
#define DHT_START_HIGH_US  30     /* 释放总线等待 20~40us */
#define DHT_TIMEOUT_US     100    /* 等待电平超时 */
#define DHT_BIT_1_US       40     /* 高电平超过 40us 判为 1 */

/* 等待数据线达到目标电平，超时返回 -1 */
static int dht_wait_level(const gpio_bus_t *bus, uint8_t pin,
                          uint8_t level, uint32_t timeout_us)
{
    uint32_t t = 0;

    while (t < timeout_us) {
        uint8_t cur = gpio_get(bus, pin);
        if (level ? (cur != 0) : (cur == 0)) {
            return 0;
        }
        gpio_udelay(bus, 1);
        t++;
    }
    return -1;
}

/* 读取一个字节：按每位高电平脉宽判 0/1 */
static int dht_read_byte(const gpio_bus_t *bus, uint8_t pin, uint8_t *out)
{
    uint8_t b = 0;
    int i;

    for (i = 0; i < 8; i++) {
        uint32_t high = 0;
        if (dht_wait_level(bus, pin, 0, DHT_TIMEOUT_US) != 0) {
            return -1;
        }
        if (dht_wait_level(bus, pin, 1, DHT_TIMEOUT_US) != 0) {
            return -1;
        }
        while (high < DHT_TIMEOUT_US && gpio_get(bus, pin) != 0) {
            gpio_udelay(bus, 1);
            high++;
        }
        b = (uint8_t)((b << 1) | (high > DHT_BIT_1_US ? 1u : 0u));
    }
    *out = b;
    return 0;
}

/* 完成一次完整通信，读回 40 位（5 字节） */
static int dht_read_raw(const gpio_bus_t *bus, uint8_t pin, uint8_t d[5])
{
    int i;

    gpio_set(bus, pin, 0);
    gpio_udelay(bus, DHT_START_LOW_US);
    gpio_set(bus, pin, 1);
    gpio_udelay(bus, DHT_START_HIGH_US);

    if (dht_wait_level(bus, pin, 0, DHT_TIMEOUT_US) != 0) {
        return -1;
    }
    if (dht_wait_level(bus, pin, 1, DHT_TIMEOUT_US) != 0) {
        return -1;
    }
    for (i = 0; i < 5; i++) {
        if (dht_read_byte(bus, pin, &d[i]) != 0) {
            return -1;
        }
    }
    return 0;
}

int dht22_init(const gpio_bus_t *bus, uint8_t pin)
{
    if (bus == NULL) {
        LOG_E("dht22", "init fail: null bus");
        return -1;
    }
    LOG_I("dht22", "init ok pin %u", (unsigned int)pin);
    return 0;
}

int dht22_read(const gpio_bus_t *bus, uint8_t pin, float *temp, float *humidity)
{
    uint8_t d[5];
    uint16_t rh;
    int16_t ts;

    if (bus == NULL || temp == NULL || humidity == NULL) {
        return -1;
    }
    if (dht_read_raw(bus, pin, d) != 0) {
        LOG_E("dht22", "read timeout");
        return -1;
    }
    /* 校验：前 4 字节累加低 8 位等于第 5 字节 */
    if ((uint8_t)(d[0] + d[1] + d[2] + d[3]) != d[4]) {
        LOG_E("dht22", "checksum fail");
        return -2;
    }
    /* 湿度 16bit 大端/10；温度 16bit 有符号/10（bit15 为符号） */
    rh = (uint16_t)(((uint16_t)d[0] << 8) | d[1]);
    ts = (int16_t)(((uint16_t)d[2] << 8) | d[3]);
    *humidity = rh / 10.0f;
    *temp = ts / 10.0f;
    return 0;
}