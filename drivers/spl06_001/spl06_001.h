#ifndef SPL06_001_H
#define SPL06_001_H

/*
 * SPL06-001 气压 / 温度传感器驱动（I2C）。
 *
 * 参考 libdriver 风格：
 *   spl06_001_init / spl06_001_deinit / spl06_001_read / spl06_001_read_raw
 *
 * 初始化时校验产品 ID（0x10）并读取 0x10 起 18 字节校准系数；
 * 读取时先按过采样配置把 24 位原始值缩放到物理量纲，
 * 再代入 datasheet 补偿多项式换算为 ℃ 与 Pa。支持多实例。
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int16_t c0;
    int16_t c1;
    int32_t c00;
    int32_t c10;
    int16_t c01;
    int16_t c11;
    int16_t c20;
    int16_t c21;
    int16_t c30;
} spl06_001_calib_t;

typedef struct {
    driver_i2c_t i2c;
    spl06_001_calib_t calib;
    float kp;           /* 压力缩放因子（由过采样决定） */
    float kt;           /* 温度缩放因子 */
    bool initialized;
} spl06_001_handle_t;

typedef struct {
    float temperature;  /* ℃ */
    float pressure;     /* Pa */
} spl06_001_data_t;

/** 初始化：校验产品 ID 0x10，读校准系数并启动连续温压测量 */
int spl06_001_init(spl06_001_handle_t *handle, port_i2c_handle_t *i2c, uint16_t address);

/** 反初始化（停止测量，不关闭总线） */
int spl06_001_deinit(spl06_001_handle_t *handle);

/** 读取一次温度 ℃ 与气压 Pa */
int spl06_001_read(spl06_001_handle_t *handle, spl06_001_data_t *data);

/** 读取原始 24 位补码值，任一指针可为 NULL */
int spl06_001_read_raw(spl06_001_handle_t *handle, int32_t *raw_temperature,
                       int32_t *raw_pressure);

/** 读取产品 ID，正常为 0x10 */
int spl06_001_get_chip_id(spl06_001_handle_t *handle, uint8_t *chip_id);

#ifdef __cplusplus
}
#endif

#endif /* SPL06_001_H */
