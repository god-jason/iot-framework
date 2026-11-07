#ifndef BMP388_H
#define BMP388_H

/*
 * BMP388 气压 / 温度传感器驱动（I2C 或 SPI）。
 *
 * 参考 libdriver 风格：
 *   bmp388_init / bmp388_init_spi / bmp388_deinit / bmp388_read
 *
 * 24 位原始数据，初始化时读取 0x31 起 20 字节校准系数，
 * 读取时按 datasheet 双精度补偿公式换算为 ℃ 与 Pa。支持多实例。
 *
 * 用法（I2C）：
 *   bmp388_handle_t bmp388;
 *   bmp388_init(&bmp388, &i2c1, BMP388_DEFAULT_ADDRESS);
 *   bmp388_data_t d;
 *   bmp388_read(&bmp388, &d);
 *
 * 用法（SPI）：
 *   bmp388_init_spi(&bmp388, &spi1, IOT_PIN(0, 4));
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    double t1;
    double t2;
    double t3;
    double p1;
    double p2;
    double p3;
    double p4;
    double p5;
    double p6;
    double p7;
    double p8;
    double p9;
    double p10;
    double p11;
} bmp388_calib_t;

typedef struct {
    driver_i2c_t i2c;
    driver_spi_t spi;
    bool use_spi;
    bmp388_calib_t calib;
    double t_lin;       /* 温度补偿中间量（℃），压力补偿需要 */
    bool initialized;
} bmp388_handle_t;

typedef struct {
    float temperature;  /* ℃ */
    float pressure;     /* Pa */
} bmp388_data_t;

/** 初始化（I2C）：校验 chip id 0x50、软复位、读校准、配置过采样 */
int bmp388_init(bmp388_handle_t *handle, iot_i2c_t *i2c, uint16_t address);

/** 初始化（SPI，4 线模式，cs 为片选引脚） */
int bmp388_init_spi(bmp388_handle_t *handle, iot_spi_t *spi, iot_pin_t cs);

/** 反初始化（切回 sleep，不关闭总线） */
int bmp388_deinit(bmp388_handle_t *handle);

/** 读取一次温度 ℃ 与气压 Pa（内部完成测量等待与补偿） */
int bmp388_read(bmp388_handle_t *handle, bmp388_data_t *data);

/** 读取原始 24 位 ADC 值，任一指针可为 NULL */
int bmp388_read_raw(bmp388_handle_t *handle, uint32_t *raw_temperature,
                    uint32_t *raw_pressure);

/** 读取芯片 ID，正常为 0x50 */
int bmp388_get_chip_id(bmp388_handle_t *handle, uint8_t *chip_id);

#ifdef __cplusplus
}
#endif

#endif /* BMP388_H */
