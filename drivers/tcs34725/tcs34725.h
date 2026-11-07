#ifndef TCS34725_H
#define TCS34725_H

/*
 * TCS34725 RGB 颜色传感器驱动（I2C）。
 *
 * 输出 RGBC（Clear 为无滤波通道）原始计数，并可给出估算 lux。
 * 积分时间（ATIME）与增益（1x/4x/16x/60x）均可配置。
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    TCS34725_GAIN_1X = 0,
    TCS34725_GAIN_4X,
    TCS34725_GAIN_16X,
    TCS34725_GAIN_60X
} tcs34725_gain_t;

typedef struct {
    uint16_t clear;
    uint16_t red;
    uint16_t green;
    uint16_t blue;
    bool saturated;
} tcs34725_rgbc_t;

typedef struct {
    tcs34725_rgbc_t rgbc;
    float lux;           /* 估算值，系数需按光学窗口标定 */
} tcs34725_data_t;

typedef struct {
    driver_i2c_t i2c;
    uint8_t atime;
    tcs34725_gain_t gain;
    bool initialized;
} tcs34725_handle_t;

/** 初始化：probe + 校验 ID + 配置 ATIME/增益 + 使能 RGBC */
int tcs34725_init(tcs34725_handle_t *handle, iot_i2c_t *i2c, uint16_t address);
int tcs34725_deinit(tcs34725_handle_t *handle);

/** 设置积分时间寄存器值：越小积分时间越长（2.4ms ~ 614.4ms） */
int tcs34725_set_atime(tcs34725_handle_t *handle, uint8_t atime);

int tcs34725_set_gain(tcs34725_handle_t *handle, tcs34725_gain_t gain);

/** 当前 ATIME 对应的积分时间（ms） */
float tcs34725_get_integration_time_ms(const tcs34725_handle_t *handle);

/** 读取 RGBC 原始计数 */
int tcs34725_read_raw(tcs34725_handle_t *handle, tcs34725_rgbc_t *rgbc);

/** 读取 RGBC 并给出估算 lux */
int tcs34725_read(tcs34725_handle_t *handle, tcs34725_data_t *data);

#ifdef __cplusplus
}
#endif

#endif /* TCS34725_H */
