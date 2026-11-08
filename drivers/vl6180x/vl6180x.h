#ifndef VL6180X_H
#define VL6180X_H

/*
 * VL6180X ToF 测距 / 接近传感器驱动（I2C，默认地址 0x29，量程约 20cm 内精度最高，
 * 最远约 60cm~1m）。
 *
 * 实现等级：基础。init 做 model id(0x0000=0xB4) 校验、FRESH_OUT_OF_RESET 处理
 * 与 range 中断配置；read 单次测距后读取距离。
 * ST 官方私有初始化参数（Private Settings，首次上电必须写入）未移植，
 * 详见 vl6180x.c 注释——需按 datasheet / ST API 补充。
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    driver_i2c_t i2c;
    bool needs_private_settings;  /* 首次上电为 true：建议上层补充私有初始化参数 */
    bool initialized;
} vl6180x_handle_t;

typedef struct {
    uint16_t distance_mm;  /* 距离，mm（0~255） */
    uint8_t range_status;  /* RESULT__RANGE_STATUS 低 4 位，0 = 无错误 */
} vl6180x_data_t;

/** @param address 7 位地址，传 0 取 0x29 */
int vl6180x_init(vl6180x_handle_t *handle, hal_i2c_t *i2c, uint16_t address);

int vl6180x_deinit(vl6180x_handle_t *handle);

/** 单次测距（阻塞，最长约 VL6180X_RANGE_TIMEOUT_MS） */
int vl6180x_read(vl6180x_handle_t *handle, vl6180x_data_t *data);

/** 仅取距离；range_status 非 0 时仍填充距离并返回 HAL_NOT_READY */
int vl6180x_read_distance(vl6180x_handle_t *handle, uint16_t *distance_mm);

#ifdef __cplusplus
}
#endif

#endif /* VL6180X_H */
