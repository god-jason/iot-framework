#ifndef BMP390_H
#define BMP390_H

/*
 * BMP390 气压 / 温度传感器驱动（I2C 或 SPI）。
 *
 * 与 BMP388 共用同一套寄存器映射与补偿算法，仅 chip id 不同（0x60）：
 *   bmp390_init / bmp390_init_spi / bmp390_deinit / bmp390_read
 *
 * 24 位原始数据，初始化时读取 0x31 起 20 字节校准系数，
 * 读取时按 datasheet 双精度补偿公式换算为 ℃ 与 Pa。支持多实例。
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
} bmp390_calib_t;

typedef struct {
    driver_i2c_t i2c;
    driver_spi_t spi;
    bool use_spi;
    bmp390_calib_t calib;
    double t_lin;       /* 温度补偿中间量（℃），压力补偿需要 */
    bool initialized;
} bmp390_handle_t;

typedef struct {
    float temperature;  /* ℃ */
    float pressure;     /* Pa */
} bmp390_data_t;

/** 初始化（I2C）：校验 chip id 0x60、软复位、读校准、配置过采样 */
int bmp390_init(bmp390_handle_t *handle, iot_i2c_t *i2c, uint16_t address);

/** 初始化（SPI，4 线模式，cs 为片选引脚） */
int bmp390_init_spi(bmp390_handle_t *handle, iot_spi_t *spi, iot_pin_t cs);

/** 反初始化（切回 sleep，不关闭总线） */
int bmp390_deinit(bmp390_handle_t *handle);

/** 读取一次温度 ℃ 与气压 Pa（内部完成测量等待与补偿） */
int bmp390_read(bmp390_handle_t *handle, bmp390_data_t *data);

/** 读取原始 24 位 ADC 值，任一指针可为 NULL */
int bmp390_read_raw(bmp390_handle_t *handle, uint32_t *raw_temperature,
                    uint32_t *raw_pressure);

/** 读取芯片 ID，正常为 0x60 */
int bmp390_get_chip_id(bmp390_handle_t *handle, uint8_t *chip_id);

#ifdef __cplusplus
}
#endif

#endif /* BMP390_H */
