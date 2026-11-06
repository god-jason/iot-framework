/*
 * SRF05 超声波测距模块驱动。
 * 引脚：TRIG 触发（推挽输出）、ECHO 回响（输入）、另有 MODE 模式引脚。
 * 模式引脚说明：MODE 引脚悬空（默认）时工作在 2 线触发模式，接口与
 * HC-SR04 一致，TRIG 拉高 10us 触发、ECHO 输出高电平；本驱动按该
 * 2 线模式实现。若 MODE 引脚接地则切换为单线模式（触发与回响共用
 * 一根线），此处不实现。
 * 测量原理：distance_cm = 高电平时间(us) / 58。
 *
 * 说明：本驱动用 GPIO 轮询 + 微秒延时近似测量高电平时长，
 * 实际应用建议改用硬件定时器输入捕获或中断计时，精度更高。
 */
#include "iot_sensor_srf05.h"
#include "iot_logger.h"
#include <stddef.h>

#define SRF05_TRIG_US       10u    /* 触发脉冲宽度 */
#define SRF05_ECHO_TIMEOUT  30000u /* 等待与测量超时（us） */

int iot_srf05_init(iot_gpio_bus_t *bus, uint8_t trig, uint8_t echo)
{
    if (bus == NULL) {
        LOG_E("srf05", "bus null");
        return -1;
    }
    (void)echo;
    iot_gpio_set(bus, trig, 0);
    LOG_I("srf05", "init ok");
    return 0;
}

int iot_srf05_measure(iot_gpio_bus_t *bus, uint8_t trig, uint8_t echo, float *distance)
{
    uint32_t us_count = 0;
    uint32_t wait = 0;

    if (bus == NULL || distance == NULL) {
        LOG_E("srf05", "bad arg");
        return -1;
    }

    /* 确保 TRIG 空闲为低 */
    iot_gpio_set(bus, trig, 0);
    iot_gpio_udelay(bus, 2);

    /* 发送触发脉冲：拉高 10us 后拉低 */
    iot_gpio_set(bus, trig, 1);
    iot_gpio_udelay(bus, SRF05_TRIG_US);
    iot_gpio_set(bus, trig, 0);

    /* 等待 ECHO 上升沿 */
    while (iot_gpio_get(bus, echo) == 0) {
        if (++wait > SRF05_ECHO_TIMEOUT) {
            LOG_E("srf05", "echo no rising edge");
            return -2;
        }
        iot_gpio_udelay(bus, 1);
    }

    /* 测量高电平持续时间 */
    while (iot_gpio_get(bus, echo) != 0) {
        iot_gpio_udelay(bus, 1);
        if (++us_count > SRF05_ECHO_TIMEOUT) {
            LOG_E("srf05", "echo timeout");
            break;
        }
    }

    /* distance_cm = us / 58 */
    *distance = (float)us_count / 58.0f;
    return 0;
}