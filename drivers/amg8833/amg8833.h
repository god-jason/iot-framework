#ifndef AMG8833_H
#define AMG8833_H

/*
 * Panasonic AMG8833 8x8 红外热像阵列驱动（I2C）。
 *
 * 参考 libdriver 风格：
 *   amg8833_init / amg8833_deinit / amg8833_read / amg8833_write
 * 像素数据：12 位有符号，0.25℃/LSB；片上热敏电阻 0.0625℃/LSB。
 */

#include "common/driver_io.h"
#include "amg8833_register.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AMG8833_FRAME_RATE_10FPS = 0,
    AMG8833_FRAME_RATE_1FPS
} amg8833_frame_rate_e;

typedef struct {
    driver_i2c_t i2c;
    amg8833_frame_rate_e frame_rate;
    bool initialized;
} amg8833_handle_t;

typedef struct {
    int16_t pixels[AMG8833_PIXEL_COUNT]; /* 原始 12 位有符号值 */
    int16_t thermistor;                  /* 原始热敏电阻值 */
} amg8833_raw_frame_t;

/**
 * @brief  初始化 AMG8833
 * @param  i2c      已初始化的 I2C 主机句柄
 * @param  address  7 位地址，0 表示使用默认 0x69
 */
int amg8833_init(amg8833_handle_t *handle, hal_i2c_t *i2c, uint16_t address);

int amg8833_deinit(amg8833_handle_t *handle);

/** 读取一帧：pixels 必须能存放 AMG8833_PIXEL_COUNT 个 float；thermistor_c 可为 NULL */
int amg8833_read(amg8833_handle_t *handle, float *pixels, float *thermistor_c);

/** 读取原始值（不做单位换算） */
int amg8833_read_raw(amg8833_handle_t *handle, amg8833_raw_frame_t *frame);

/** 仅读取片上热敏电阻温度（℃） */
int amg8833_read_thermistor(amg8833_handle_t *handle, float *celsius);

/** 通用寄存器写（数据可为 1 字节或多字节） */
int amg8833_write(amg8833_handle_t *handle, uint8_t reg, const uint8_t *data, size_t len);

/** 通用寄存器读 */
int amg8833_read_reg(amg8833_handle_t *handle, uint8_t reg, uint8_t *data, size_t len);

/** 设置帧率 */
int amg8833_set_frame_rate(amg8833_handle_t *handle, amg8833_frame_rate_e rate);

/** 使能/关闭中断输出，enable 为 true 时按 high_c/low_c 设置绝对阈值 */
int amg8833_set_interrupt(amg8833_handle_t *handle, bool enable, float high_c, float low_c);

/** 读取并清除状态字节（溢出 / 中断发生） */
int amg8833_read_status(amg8833_handle_t *handle, uint8_t *status);

#ifdef __cplusplus
}
#endif

#endif /* AMG8833_H */
