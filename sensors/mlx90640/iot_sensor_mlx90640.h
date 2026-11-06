#ifndef IOT_SENSOR_MLX90640_H
#define IOT_SENSOR_MLX90640_H

// MLX90640 红外热成像阵列（I2C 0x33，32x24 共 768 像素）
#include "bus/iot_bus_i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

#define IOT_MLX90640_PIXELS 768  // 32x24

// 初始化：探测器件（完整 EEPROM 校准读取见 .c 说明）
int iot_mlx90640_init(const iot_i2c_bus_t *bus);
// 读取单个像素温度（addr 为 0-767 像素序号），单位 ℃
int iot_mlx90640_read_pixel(const iot_i2c_bus_t *bus, uint16_t addr, float *temp);
// 读取整帧（len 个像素，不超过 768），单位 ℃
int iot_mlx90640_read(const iot_i2c_bus_t *bus, float *frame, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif