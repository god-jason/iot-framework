#ifndef OPT3001_H
#define OPT3001_H

/*
 * OPT3001 环境光传感器驱动（I2C）。
 *
 * 输出直接为 lux：RESULT 高 12 位尾数 + 低 4 位指数，
 * lux = mantissa * 0.01 * 2^exponent，无需外部系数。
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    OPT3001_CONV_100MS = 0,   /* 100 ms，高速 */
    OPT3001_CONV_800MS        /* 800 ms，高分辨率 / 低照度 */
} opt3001_conv_time_t;

typedef struct {
    driver_i2c_t i2c;
    opt3001_conv_time_t conv_time;
    bool continuous;
    bool initialized;
} opt3001_handle_t;

/** 初始化：probe + 校验 DEVICE_ID(0x3001) + 配置为连续转换 */
int opt3001_init(opt3001_handle_t *handle, iot_i2c_t *i2c, uint16_t address);
int opt3001_deinit(opt3001_handle_t *handle);

int opt3001_set_conversion_time(opt3001_handle_t *handle, opt3001_conv_time_t conv_time);

/** 连续 / 单次转换模式切换 */
int opt3001_set_continuous(opt3001_handle_t *handle, bool enable);

/** 读取光照强度（lux） */
int opt3001_read(opt3001_handle_t *handle, float *lux);

/** 读取原始 RESULT 寄存器并给出换算后的 lux */
int opt3001_read_raw(opt3001_handle_t *handle, uint16_t *raw, float *lux);

/** 读取器件 ID，正常应为 OPT3001_DEVICE_ID */
int opt3001_get_device_id(opt3001_handle_t *handle, uint16_t *device_id);

#ifdef __cplusplus
}
#endif

#endif /* OPT3001_H */
