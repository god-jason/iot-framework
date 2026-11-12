#ifndef BMP280_H
#define BMP280_H

/*
 * BMP280 气压/温度传感器驱动（I2C）。
 *
 * 参考 libdriver 设计，采用 Bosch 官方定点补偿算法：
 *   bmp280_init / bmp280_deinit / bmp280_read / bmp280_read_raw
 *
 * 校准系数保存在句柄内，支持多实例。
 */

#include "hal.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t t1;
    int16_t t2;
    int16_t t3;

    uint16_t p1;
    int16_t p2;
    int16_t p3;
    int16_t p4;
    int16_t p5;
    int16_t p6;
    int16_t p7;
    int16_t p8;
    int16_t p9;
} bmp280_calib_t;

typedef struct {
    hal_i2c_dev_t i2c;
    bmp280_calib_t calib;
    int32_t t_fine;
    bool initialized;
} bmp280_handle_t;

typedef struct {
    float temperature;   /* ℃ */
    float pressure;      /* Pa */
} bmp280_data_t;

int bmp280_init(bmp280_handle_t *handle, hal_i2c_t *i2c, uint16_t address);
int bmp280_deinit(bmp280_handle_t *handle);

/** 读取一次温压（内部完成转换等待） */
int bmp280_read(bmp280_handle_t *handle, bmp280_data_t *data);

/** 读取原始 ADC 值，便于上层自行处理 */
int bmp280_read_raw(bmp280_handle_t *handle, int32_t *raw_temperature,
                    int32_t *raw_pressure);

/** 读取芯片 ID，正常应为 0x58 */
int bmp280_get_chip_id(bmp280_handle_t *handle, uint8_t *chip_id);

#ifdef __cplusplus
}
#endif

#endif /* BMP280_H */
