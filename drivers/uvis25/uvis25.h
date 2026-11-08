#ifndef UVIS25_H
#define UVIS25_H

/*
 * UVIS25 紫外线指数传感器驱动（I2C）。
 *
 * 输出 UV 指数（UVI）与 16 位原始计数，支持 ODR 配置。
 * 注意：UV 指数换算比例默认按 100 LSB = 1 UVI 估算，
 *       量产前需按 datasheet 灵敏度 / 标定结果确认。
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UVIS25_ODR_1HZ = 0,
    UVIS25_ODR_7HZ,
    UVIS25_ODR_12HZ
} uvis25_odr_t;

typedef struct {
    driver_i2c_t i2c;
    uvis25_odr_t odr;
    bool initialized;
} uvis25_handle_t;

/** 初始化：probe + 校验 WHO_AM_I + 打开传感器并配置 ODR */
int uvis25_init(uvis25_handle_t *handle, hal_i2c_t *i2c, uint16_t address);
int uvis25_deinit(uvis25_handle_t *handle);

int uvis25_set_odr(uvis25_handle_t *handle, uvis25_odr_t odr);

/** 读取 UV 指数原始值（16 位无符号） */
int uvis25_read_raw(uvis25_handle_t *handle, uint16_t *raw);

/** 读取 UV 指数（UVI），系数需按 datasheet / 标定确认 */
int uvis25_read(uvis25_handle_t *handle, float *uv_index);

/** 读取 WHO_AM_I，正常应为 0xCA */
int uvis25_get_who_am_i(uvis25_handle_t *handle, uint8_t *who_am_i);

#ifdef __cplusplus
}
#endif

#endif /* UVIS25_H */
