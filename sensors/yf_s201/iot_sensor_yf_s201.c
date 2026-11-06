/*
 * YF-S201 霍尔水流量传感器驱动。
 * 输出：方波脉冲，约 450 脉冲/升，即 7.5 脉冲/秒对应 1 L/min。
 * 原理：流量(L/min) = 单位时间脉冲数(Hz) / 7.5。
 *
 * 说明：本驱动用 GPIO 轮询 + 微秒延时近似实现单位时间脉冲计数；
 * 实际应用建议改用硬件定时器 + 外部中断计数，精度更高，且不会阻塞。
 */
#include "iot_sensor_yf_s201.h"
#include "iot_logger.h"
#include <stddef.h>

#define YF_S201_PULSES_PER_LITER  7.5f      /* 1 L/min 对应 7.5 脉冲/秒 */
#define YF_S201_WINDOW_US         1000000u  /* 采样窗 1s（近似） */
#define YF_S201_SAMPLE_US         100u      /* 轮询采样间隔 */

int iot_flow_init(iot_gpio_bus_t *bus, uint8_t pin)
{
    if (bus == NULL) {
        LOG_E("yf_s201", "bus null");
        return -1;
    }
    (void)pin;
    LOG_I("yf_s201", "init ok");
    return 0;
}

int iot_flow_read(iot_gpio_bus_t *bus, uint8_t pin, float *liters_per_min)
{
    uint32_t pulses = 0;
    uint32_t elapsed = 0;
    uint8_t last;
    uint8_t cur;

    if (bus == NULL || liters_per_min == NULL) {
        LOG_E("yf_s201", "bad arg");
        return -1;
    }

    last = iot_gpio_get(bus, pin);
    while (elapsed < YF_S201_WINDOW_US) {
        iot_gpio_udelay(bus, YF_S201_SAMPLE_US);
        elapsed += YF_S201_SAMPLE_US;
        cur = iot_gpio_get(bus, pin);
        if (cur != 0 && last == 0) {
            pulses++; /* 上升沿计数 */
        }
        last = cur;
    }

    /* 流量(L/min) = 脉冲数(1s 内) / 7.5 */
    *liters_per_min = (float)pulses / YF_S201_PULSES_PER_LITER;
    return 0;
}