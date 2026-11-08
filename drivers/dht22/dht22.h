#ifndef DHT22_H
#define DHT22_H

/*
 * DHT22 温湿度传感器驱动（单总线，GPIO 软件时序）。
 *
 * 参考 libdriver 设计：
 *   dht22_init / dht22_deinit / dht22_read / dht22_read_raw
 *
 * 硬件访问只经由 common/driver_io.h 暴露的 iot_gpio_* 与 driver_delay_us：
 * 空闲态为输入 + 上拉；起始信号期间临时切为推挽输出拉低，
 * 释放后立即切回输入读取从机电平（不依赖开漏输出能力）。
 * 引脚必须外接 4.7k~10k 上拉电阻。
 *
 * 位时序对中断敏感，整帧收发在 hal_irq_disable/enable 临界区内完成
 * （driver_io.h 未提供中断接口，沿用 ds18b20/dht11 的既有做法）。
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    hal_pin_t pin;          /* 单总线数据引脚 */
    uint32_t last_read_ms;   /* 上次成功通信的开始时刻，用于限制采样间隔 */
    bool sampled;            /* last_read_ms 是否有效（避免 tick 0 的歧义） */
    bool initialized;
} dht22_handle_t;

/**
 * @brief  初始化 DHT22（引脚配置为开漏输出 + 上拉，总线置为空闲高电平）
 * @param  handle  驱动句柄
 * @param  pin     数据引脚（HAL_PIN(port, index) 编码）
 * @return 0 成功，负值为 hal_status_t
 */
int dht22_init(dht22_handle_t *handle, hal_pin_t pin);

/**
 * @brief  反初始化（释放引脚）
 * @param  handle  驱动句柄
 * @return 0 成功，负值为 hal_status_t
 */
int dht22_deinit(dht22_handle_t *handle);

/**
 * @brief  读取温度与湿度（含校验和验证）
 * @param  handle       驱动句柄
 * @param  temperature  温度，单位 ℃，分辨率 0.1℃，支持负值，可为 NULL
 * @param  humidity     湿度，单位 %RH，分辨率 0.1%RH，可为 NULL
 * @return 0 成功；校验和错误返回 HAL_CRC_ERROR；总线无响应返回 HAL_TIMEOUT；
 *         距上次采样不足 2s 返回 HAL_BUSY
 */
int dht22_read(dht22_handle_t *handle, float *temperature, float *humidity);

/**
 * @brief  读取原始 5 字节帧（不做校验和检查）
 * @param  handle  驱动句柄
 * @param  frame   输出缓冲区，frame[0..1] 湿度、frame[2..3] 温度、frame[4] 校验和
 * @return 0 成功，负值为 hal_status_t；采样间隔不足 2s 返回 HAL_BUSY
 */
int dht22_read_raw(dht22_handle_t *handle, uint8_t frame[5]);

#ifdef __cplusplus
}
#endif

#endif /* DHT22_H */
