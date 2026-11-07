#ifndef ASF01_H
#define ASF01_H

/*
 * ASF01 空气质量传感器驱动（I2C）。
 *
 * 注意：ASF01 存在多个厂商/协议变体，本驱动只提供通用的寄存器读取与
 * 原始值接口；具体量程、标定与命令字需按所用手册在 asf01_register.h 中补充。
 *
 * 参考 libdriver 风格：asf01_init / asf01_deinit / asf01_read_raw / asf01_read。
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ASF01_DEFAULT_ADDRESS   0x5AU

typedef struct {
    driver_i2c_t i2c;
    uint16_t raw;
    float concentration;   /* ppm，按标定系数换算 */
    float scale;           /* 原始值 -> ppm 的系数 */
    bool initialized;
} asf01_handle_t;

int asf01_init(asf01_handle_t *handle, port_i2c_handle_t *i2c, uint16_t address);
int asf01_deinit(asf01_handle_t *handle);

/** 读取 2 字节原始值 */
int asf01_read_raw(asf01_handle_t *handle, uint16_t *raw);

/** 读取浓度（ppm），使用 handle->scale 换算 */
int asf01_read(asf01_handle_t *handle, float *concentration);

/** 设置原始值到 ppm 的换算系数 */
int asf01_set_scale(asf01_handle_t *handle, float scale);

#ifdef __cplusplus
}
#endif

#endif /* ASF01_H */
