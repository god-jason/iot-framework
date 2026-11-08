#ifndef VL53L0X_H
#define VL53L0X_H

/*
 * VL53L0X ToF 激光测距传感器驱动（I2C，默认地址 0x29，量程约 2m）。
 *
 * 实现等级：基础。init 做软复位与 model id(0xC0=0xEE) 校验，
 * read 触发一次单次测距并轮询中断状态后读取距离。
 * ST 官方推荐的完整初始化序列（TuningSettings / SPAD / 参考校准）
 * 尚未移植，详见 vl53l0x.c 内注释——这部分需按 datasheet / ST API 补充。
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    driver_i2c_t i2c;
    uint8_t revision;      /* 0xC2 读到的版本号，仅作诊断 */
    bool initialized;
} vl53l0x_handle_t;

typedef struct {
    uint16_t distance_mm;  /* 距离，mm（量程外为 8190 左右） */
    uint8_t range_status;  /* 0 = 数据有效，其它值见数据手册 */
} vl53l0x_data_t;

/** @param address 7 位地址，传 0 取 0x29 */
int vl53l0x_init(vl53l0x_handle_t *handle, hal_i2c_t *i2c, uint16_t address);

int vl53l0x_deinit(vl53l0x_handle_t *handle);

/** 单次测距（阻塞，最长约 VL53L0X_RANGE_TIMEOUT_MS） */
int vl53l0x_read(vl53l0x_handle_t *handle, vl53l0x_data_t *data);

/** 仅取距离；返回非 0 表示测距状态异常（数据仍已填充） */
int vl53l0x_read_distance(vl53l0x_handle_t *handle, uint16_t *distance_mm);

#ifdef __cplusplus
}
#endif

#endif /* VL53L0X_H */
