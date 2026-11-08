#ifndef DHT11_H
#define DHT11_H

/*
 * DHT11 温湿度传感器驱动（单总线，GPIO bit-bang）。
 *
 * 参考 libdriver 设计：
 *   dht11_init / dht11_deinit / dht11_read
 * 引脚操作只使用 driver_gpio_* 与 driver_delay_us。
 *
 * 注意：位时序对中断敏感，临界区通过 hal_irq_disable/enable 保护
 *       （driver_io.h 未提供中断接口，沿用 ds18b20 的既有做法）。
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    hal_pin_t pin;
    uint32_t last_read_ms;   /* 上次成功读取时刻，用于限制 1s 采样间隔 */
    bool initialized;
} dht11_handle_t;

/**
 * @brief  初始化 DHT11（配置为输入 + 上拉，等待总线空闲）
 * @param  pin  数据引脚（hal_pin_t，HAL_PIN(port, pin) 编码）
 */
int dht11_init(dht11_handle_t *handle, hal_pin_t pin);

/** @brief 反初始化（释放引脚为输入） */
int dht11_deinit(dht11_handle_t *handle);

/**
 * @brief  读取温度与湿度（含校验和验证）
 * @param  temperature  温度，单位 ℃，可为 NULL
 * @param  humidity     湿度，单位 %RH，可为 NULL
 * @note   两次读取之间需间隔 >= 1s，过早调用返回 HAL_BUSY
 */
int dht11_read(dht11_handle_t *handle, float *temperature, float *humidity);

/** @brief 读取原始 5 字节帧 */
int dht11_read_raw(dht11_handle_t *handle, uint8_t frame[5]);

#ifdef __cplusplus
}
#endif

#endif /* DHT11_H */
