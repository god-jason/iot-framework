#ifndef AM2320_H
#define AM2320_H

/*
 * AM2320 温湿度传感器驱动（I2C 0x5C，类 DHT 帧协议）。
 *
 * 参考 libdriver 设计：
 *   am2320_init / am2320_deinit / am2320_read
 * 所有总线访问经由 drivers/common/driver_io.h。
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    driver_i2c_t i2c;
    bool initialized;
} am2320_handle_t;

/**
 * @brief  初始化 AM2320（唤醒 + 试读一帧确认器件在位）
 * @param  address  7 位从机地址，0 表示使用默认 0x5C
 */
int am2320_init(am2320_handle_t *handle, port_i2c_handle_t *i2c, uint16_t address);

/** @brief 反初始化（不关闭总线） */
int am2320_deinit(am2320_handle_t *handle);

/**
 * @brief  读取温度与湿度（含 CRC16/Modbus 校验）
 * @param  temperature  温度，单位 ℃，可为 NULL
 * @param  humidity     湿度，单位 %RH，可为 NULL
 */
int am2320_read(am2320_handle_t *handle, float *temperature, float *humidity);

/** @brief 读取原始帧（8 字节，调试用） */
int am2320_read_raw(am2320_handle_t *handle, uint8_t frame[8]);

#ifdef __cplusplus
}
#endif

#endif /* AM2320_H */
