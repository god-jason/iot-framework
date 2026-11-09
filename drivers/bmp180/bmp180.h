#ifndef BMP180_H
#define BMP180_H

/*
 * BMP180 气压 / 温度传感器驱动（I2C）。
 *
 * 参考 libdriver 风格：
 *   bmp180_init / bmp180_deinit / bmp180_read / bmp180_read_raw
 *
 * 初始化时读取 0xAA 起 22 字节校准系数并保存到句柄内，
 * 读取时按 datasheet 的一阶补偿公式换算为 ℃ 与 Pa，支持多实例。
 *
 * 用法：
 *   bmp180_handle_t bmp180;
 *   bmp180_init(&bmp180, &i2c1, BMP180_DEFAULT_ADDRESS);
 *   bmp180_data_t d;
 *   bmp180_read(&bmp180, &d);
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int16_t  ac1;    /* 压力灵敏度 */
    int16_t  ac2;    /* 压力偏置 */
    int16_t  ac3;    /* 压力灵敏度温度系数 */
    uint16_t ac4;    /* 压力偏置温度系数 */
    uint16_t ac5;    /* 参考温度 */
    uint16_t ac6;    /* 温度系数 */
    int16_t  b1;
    int16_t  b2;
    int16_t  mb;
    int16_t  mc;
    int16_t  md;
} bmp180_calib_t;

typedef struct {
    driver_i2c_t i2c;
    bmp180_calib_t calib;
    uint8_t oversampling;   /* 0..3，见 BMP180_OSS_* */
    int32_t b5;             /* 温度补偿中间量，压力补偿复用 */
    bool initialized;
} bmp180_handle_t;

typedef struct {
    float temperature;      /* ℃ */
    float pressure;         /* Pa */
} bmp180_data_t;

/**
 * @brief  初始化 BMP180
 * @param  handle   驱动句柄
 * @param  i2c      已初始化的 I2C 主机句柄
 * @param  address  7 位从机地址，0 表示使用 0x77
 * @return 0 成功，负值为 HAL 状态码
 */
int bmp180_init(bmp180_handle_t *handle, hal_i2c_t *i2c, uint16_t address);

/** 反初始化（不关闭总线） */
int bmp180_deinit(bmp180_handle_t *handle);

/** 设置压力过采样（BMP180_OSS_*），需在 init 之后调用 */
int bmp180_set_oversampling(bmp180_handle_t *handle, uint8_t oss);

/** 读取一次温度 ℃ 与气压 Pa（内部完成转换等待与补偿） */
int bmp180_read(bmp180_handle_t *handle, bmp180_data_t *data);

/** 读取原始 UT / UP（16 位 / 19 位），任一指针可为 NULL */
int bmp180_read_raw(bmp180_handle_t *handle, int32_t *ut, int32_t *up);

/** 读取芯片 ID，正常为 0x55 */
int bmp180_get_chip_id(bmp180_handle_t *handle, uint8_t *chip_id);

#ifdef __cplusplus
}
#endif

#endif /* BMP180_H */
