/*
 * HC-SR04 超声波测距模块驱动。
 * 引脚：TRIG 触发（推挽输出）、ECHO 回响（输入）。
 * 测量原理：TRIG 拉高 10us 触发一次测距，模块在 ECHO 上输出
 * 与距离成正比的高电平；声速 340m/s，声波往返双程，
 * distance_cm = 高电平时间(us) / 58。
 *
 * 说明：本驱动用 GPIO 轮询 + 微秒延时近似测量高电平时长，
 * 实际应用建议改用硬件定时器输入捕获或中断计时，精度更高。
 */
#include "hc_sr04.h"
#include "iot_logger.h"
#include <stddef.h>

#define HC_SR04_TRIG_US       10u    /* 触发脉冲宽度 */
#define HC_SR04_ECHO_TIMEOUT  30000u /* 等待与测量超时（us） */

int ultrasonic_init(gpio_bus_t *bus, uint8_t trig, uint8_t echo)
{
    if (bus == NULL) {
        LOG_E("hc_sr04", "bus null");
        return -1;
    }
    (void)echo;
    gpio_set(bus, trig, 0);
    LOG_I("hc_sr04", "init ok");
    return 0;
}

int ultrasonic_measure(gpio_bus_t *bus, uint8_t trig, uint8_t echo, float *distance)
{
    uint32_t us_count = 0;
    uint32_t wait = 0;

    if (bus == NULL || distance == NULL) {
        LOG_E("hc_sr04", "bad arg");
        return -1;
    }

    /* 确保 TRIG 空闲为低 */
    gpio_set(bus, trig, 0);
    gpio_udelay(bus, 2);

    /* 发送触发脉冲：拉高 10us 后拉低 */
    gpio_set(bus, trig, 1);
    gpio_udelay(bus, HC_SR04_TRIG_US);
    gpio_set(bus, trig, 0);

    /* 等待 ECHO 上升沿 */
    while (gpio_get(bus, echo) == 0) {
        if (++wait > HC_SR04_ECHO_TIMEOUT) {
            LOG_E("hc_sr04", "echo no rising edge");
            return -2;
        }
        gpio_udelay(bus, 1);
    }

    /* 测量高电平持续时间 */
    while (gpio_get(bus, echo) != 0) {
        gpio_udelay(bus, 1);
        if (++us_count > HC_SR04_ECHO_TIMEOUT) {
            LOG_E("hc_sr04", "echo timeout");
            break;
        }
    }

    /* distance_cm = us / 58（声速 340m/s 往返，1cm 约 58us） */
    *distance = (float)us_count / 58.0f;
    return 0;
}