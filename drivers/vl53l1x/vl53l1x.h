#ifndef VL53L1X_H
#define VL53L1X_H

/*
 * VL53L1X ToF 激光测距传感器驱动（I2C，默认地址 0x29，长距模式约 4m）。
 *
 * 实现等级：基础。init 做软复位、固件启动等待与 model id(0x010F=0xEA) 校验；
 * read 以 DSS 模式启动一次测距，轮询中断状态后读取距离。
 * ST 官方初始化中的 ROI / timing budget / 相位校准配置未移植，
 * 详见 vl53l1x.c 注释——需按 datasheet / ST API 补充。
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    driver_i2c_t i2c;
    bool initialized;
} vl53l1x_handle_t;

typedef struct {
    uint16_t distance_mm;  /* 距离，mm（无有效目标时为 8190 左右） */
    uint8_t range_status;  /* 原始状态字节低 5 位，0 = 有效 */
} vl53l1x_data_t;

/** @param address 7 位地址，传 0 取 0x29 */
int vl53l1x_init(vl53l1x_handle_t *handle, iot_i2c_t *i2c, uint16_t address);

int vl53l1x_deinit(vl53l1x_handle_t *handle);

/** 单次测距（阻塞，最长约 VL53L1X_RANGE_TIMEOUT_MS） */
int vl53l1x_read(vl53l1x_handle_t *handle, vl53l1x_data_t *data);

/** 仅取距离；range_status 非 0 时仍填充距离并返回 IOT_NOT_READY */
int vl53l1x_read_distance(vl53l1x_handle_t *handle, uint16_t *distance_mm);

#ifdef __cplusplus
}
#endif

#endif /* VL53L1X_H */
