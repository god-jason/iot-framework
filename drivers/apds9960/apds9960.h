#ifndef APDS9960_H
#define APDS9960_H

/*
 * APDS-9960 接近 / RGB 颜色传感器驱动（I2C）。
 *
 * 已实现：RGBC 原始通道读取、接近（proximity）读取、ALS 增益与积分时间配置、
 *         接近脉冲数与脉宽配置。手势引擎（Gesture FIFO）未实现。
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    APDS9960_AGAIN_1X = 0,
    APDS9960_AGAIN_4X,
    APDS9960_AGAIN_16X,
    APDS9960_AGAIN_64X
} apds9960_gain_t;

typedef struct {
    uint16_t clear;
    uint16_t red;
    uint16_t green;
    uint16_t blue;
    bool saturated;
} apds9960_rgbc_t;

typedef struct {
    apds9960_rgbc_t rgbc;
    uint8_t proximity;      /* 0~255，越大越近 */
    bool proximity_saturated;
} apds9960_data_t;

typedef struct {
    driver_i2c_t i2c;
    uint8_t atime;
    apds9960_gain_t gain;
    uint8_t pulse_count;    /* 1~64 */
    uint8_t pulse_len;      /* APDS9960_PPULSE_LEN_xxx */
    bool initialized;
} apds9960_handle_t;

/** 初始化：probe + 校验 ID(0xA8) + 打开 PON/ALS/接近 */
int apds9960_init(apds9960_handle_t *handle, port_i2c_handle_t *i2c, uint16_t address);
int apds9960_deinit(apds9960_handle_t *handle);

/** 设置 ALS 积分时间寄存器值（(256-ATIME)*2.78ms） */
int apds9960_set_atime(apds9960_handle_t *handle, uint8_t atime);

int apds9960_set_als_gain(apds9960_handle_t *handle, apds9960_gain_t gain);

/** 设置接近脉冲数与脉宽（脉宽取 APDS9960_PPULSE_LEN_xxx） */
int apds9960_set_proximity_pulse(apds9960_handle_t *handle, uint8_t pulse_count,
                                 uint8_t pulse_len);

/** 当前 ATIME 对应的积分时间（ms） */
float apds9960_get_integration_time_ms(const apds9960_handle_t *handle);

/** 读取 RGBC 通道原始计数 */
int apds9960_read_rgbc(apds9960_handle_t *handle, apds9960_rgbc_t *rgbc);

/** 读取接近值（0~255），saturated 指示是否达到满量程 0xFF */
int apds9960_read_proximity(apds9960_handle_t *handle, uint8_t *proximity, bool *saturated);

/** 一次性读取 RGBC + 接近 */
int apds9960_read(apds9960_handle_t *handle, apds9960_data_t *data);

#ifdef __cplusplus
}
#endif

#endif /* APDS9960_H */
