#ifndef AM2302_H
#define AM2302_H

/*
 * AM2302 温湿度传感器驱动（单总线，GPIO 软件时序）。
 *
 * AM2302 与 DHT22 是同一协议（湿度 16bit/10，温度 16bit 有符号/10），
 * 本驱动为 dht22 的薄封装：不复制任何时序代码，句柄内嵌 dht22_handle_t，
 * 所有 API 直接委托给 dht22_*，避免两份位时序实现维护分裂。
 *
 * 硬件访问只经由 common/driver_io.h（iot_gpio_* + driver_delay_us），
 * 数据引脚空闲为输入 + 上拉，起始信号期间临时切为推挽输出，
 * 需外接 4.7k~10k 上拉电阻。
 */

#include "../dht22/dht22.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    dht22_handle_t dev;      /* 底层 DHT22 驱动实例 */
} am2302_handle_t;

/**
 * @brief  初始化 AM2302（引脚配置为开漏输出 + 上拉）
 * @param  handle  驱动句柄
 * @param  pin     数据引脚（IOT_PIN(port, index) 编码）
 * @return 0 成功，负值为 iot_status_t
 */
int am2302_init(am2302_handle_t *handle, iot_pin_t pin);

/**
 * @brief  反初始化（释放引脚）
 * @return 0 成功，负值为 iot_status_t
 */
int am2302_deinit(am2302_handle_t *handle);

/**
 * @brief  读取温度与湿度（含校验和验证）
 * @param  temperature  温度，单位 ℃，分辨率 0.1℃，支持负值，可为 NULL
 * @param  humidity     湿度，单位 %RH，分辨率 0.1%RH，可为 NULL
 * @return 0 成功；校验和错误返回 IOT_CRC_ERROR；总线无响应返回 IOT_TIMEOUT；
 *         距上次采样不足 2s 返回 IOT_BUSY
 */
int am2302_read(am2302_handle_t *handle, float *temperature, float *humidity);

/**
 * @brief  读取原始 5 字节帧（不做校验和检查）
 * @param  frame  frame[0..1] 湿度、frame[2..3] 温度、frame[4] 校验和
 * @return 0 成功；采样间隔不足 2s 返回 IOT_BUSY
 */
int am2302_read_raw(am2302_handle_t *handle, uint8_t frame[5]);

#ifdef __cplusplus
}
#endif

#endif /* AM2302_H */
