/*
 * TCS3200 颜色传感器驱动。
 * S0/S1：输出频率分频比选择，本驱动固定 20% 分频（S0=1, S1=0）。
 * S2/S3：光电二极管滤波选择：
 *         S2=0,S3=0 红；S2=0,S3=1 蓝；S2=1,S3=1 绿；S2=1,S3=0 无滤波。
 * OUT：方波输出，频率与所选通道光强成正比；亮度越高频率越高。
 * 简化读取：分别选择 RGB 滤波器，对 OUT 脉冲计数，得到三色相对亮度。
 *
 * 说明：本驱动用 GPIO 轮询 + 微秒延时对 OUT 脉冲计数；
 * 实际应用建议改用硬件定时器测量频率，精度更高。
 */
#include "tcs3200.h"
#include "iot_logger.h"
#include <stddef.h>

#define TCS3200_WINDOW_US  10000u /* 计数窗 10ms */
#define TCS3200_SAMPLE_US  20u    /* 轮询采样步进 */

/* filter 为 2 位编码：bit1 = S2 电平，bit0 = S3 电平 */
static void tcs3200_sel(gpio_bus_t *bus, uint8_t s2, uint8_t s3, uint8_t filter)
{
    gpio_set(bus, s2, (filter >> 1) & 1u);
    gpio_set(bus, s3, filter & 1u);
}

static uint32_t tcs3200_count(gpio_bus_t *bus, uint8_t out)
{
    uint32_t count = 0;
    uint32_t elapsed = 0;
    uint8_t last = gpio_get(bus, out);
    uint8_t cur;

    while (elapsed < TCS3200_WINDOW_US) {
        gpio_udelay(bus, TCS3200_SAMPLE_US);
        elapsed += TCS3200_SAMPLE_US;
        cur = gpio_get(bus, out);
        if (cur != 0 && last == 0) {
            count++; /* 上升沿计数 */
        }
        last = cur;
    }
    return count;
}

int tcs3200_init(gpio_bus_t *bus, uint8_t s0, uint8_t s1, uint8_t s2, uint8_t s3, uint8_t out)
{
    if (bus == NULL) {
        LOG_E("tcs3200", "bus null");
        return -1;
    }
    (void)out;
    /* S0=1, S1=0：20% 输出分频（默认推荐） */
    gpio_set(bus, s0, 1);
    gpio_set(bus, s1, 0);
    /* 默认选择红色滤波器 */
    tcs3200_sel(bus, s2, s3, 0x0);
    LOG_I("tcs3200", "init ok");
    return 0;
}

int tcs3200_read_color(gpio_bus_t *bus, uint8_t s0, uint8_t s1, uint8_t s2, uint8_t s3,
                           uint8_t out, uint32_t *r, uint32_t *g, uint32_t *b)
{
    if (bus == NULL || r == NULL || g == NULL || b == NULL) {
        LOG_E("tcs3200", "bad arg");
        return -1;
    }

    /* 保持 20% 分频设置 */
    gpio_set(bus, s0, 1);
    gpio_set(bus, s1, 0);

    /* 红色滤波（S2=0,S3=0） */
    tcs3200_sel(bus, s2, s3, 0x0);
    *r = tcs3200_count(bus, out);

    /* 蓝色滤波（S2=0,S3=1） */
    tcs3200_sel(bus, s2, s3, 0x1);
    *b = tcs3200_count(bus, out);

    /* 绿色滤波（S2=1,S3=1） */
    tcs3200_sel(bus, s2, s3, 0x3);
    *g = tcs3200_count(bus, out);

    return 0;
}