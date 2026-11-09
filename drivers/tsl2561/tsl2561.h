#ifndef TSL2561_H
#define TSL2561_H

/*
 * TSL2561 环境光传感器驱动（I2C，可见光 + 红外双通道）。
 *
 * ch0 = 可见光 + 红外，ch1 = 红外；比值用于抑制红外分量后估算 lux。
 * 支持 13.7 / 101 / 402 ms 三档积分时间与 1x / 16x 增益。
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    TSL2561_GAIN_1X = 0,
    TSL2561_GAIN_16X
} tsl2561_gain_e;

typedef enum {
    TSL2561_INTEG_13MS = 0,
    TSL2561_INTEG_101MS,
    TSL2561_INTEG_402MS
} tsl2561_integration_e;

typedef struct {
    driver_i2c_t i2c;
    tsl2561_gain_e gain;
    tsl2561_integration_e integration;
    bool initialized;
} tsl2561_handle_t;

typedef struct {
    uint32_t ch0;        /* 可见光 + 红外原始计数（已归一化到 402ms/1x 之外为原始值） */
    uint32_t ch1;        /* 红外原始计数 */
    float lux;           /* 估算照度，需按光学窗口标定 */
    bool saturated;
} tsl2561_data_t;

/** 初始化：probe + 校验 ID + 上电 + 写入默认增益/积分时间 */
int tsl2561_init(tsl2561_handle_t *handle, hal_i2c_t *i2c, uint16_t address);
int tsl2561_deinit(tsl2561_handle_t *handle);

int tsl2561_set_gain(tsl2561_handle_t *handle, tsl2561_gain_e gain);
int tsl2561_set_integration_time(tsl2561_handle_t *handle, tsl2561_integration_e integration);

/** 读取两个通道原始计数 */
int tsl2561_read_raw(tsl2561_handle_t *handle, uint32_t *ch0, uint32_t *ch1);

/** 读取照度（同时给出原始计数） */
int tsl2561_read(tsl2561_handle_t *handle, tsl2561_data_t *data);

#ifdef __cplusplus
}
#endif

#endif /* TSL2561_H */
