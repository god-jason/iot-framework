#ifndef BH1750_H
#define BH1750_H

/*
 * BH1750FVI 环境光传感器驱动（I2C，命令式）。
 *
 * 支持连续/单次测量模式，输出单位 lux。
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BH1750_MODE_CONT_HIGH = 0,   /* 连续高分辨率 */
    BH1750_MODE_CONT_HIGH2,      /* 连续高分辨率 x2 */
    BH1750_MODE_CONT_LOW,        /* 连续低分辨率 */
    BH1750_MODE_ONE_HIGH,        /* 单次高分辨率 */
    BH1750_MODE_ONE_HIGH2,
    BH1750_MODE_ONE_LOW
} bh1750_mode_t;

typedef struct {
    driver_i2c_t i2c;
    bh1750_mode_t mode;
    bool initialized;
} bh1750_handle_t;

int bh1750_init(bh1750_handle_t *handle, iot_i2c_t *i2c, uint16_t address);
int bh1750_deinit(bh1750_handle_t *handle);

int bh1750_set_mode(bh1750_handle_t *handle, bh1750_mode_t mode);

/** 读取光照强度（lux） */
int bh1750_read(bh1750_handle_t *handle, float *lux);

/** 读取原始 16 位计数值 */
int bh1750_read_raw(bh1750_handle_t *handle, uint16_t *raw);

#ifdef __cplusplus
}
#endif

#endif /* BH1750_H */
