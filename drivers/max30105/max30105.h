#ifndef MAX30105_H
#define MAX30105_H

/*
 * MAX30105 心率 / 血氧 / 颗粒物传感器驱动（I2C，FIFO 读取）。
 *
 * 与 MAX30102 引脚与寄存器高度兼容，多出第三路 LED（GREEN）与接近阈值寄存器。
 * 参考 libdriver 风格：
 *   max30105_init / max30105_deinit / max30105_read / max30105_write
 * 心率/血氧/颗粒物数值算法由上层实现，本驱动只搬运原始 FIFO 数据；
 * 颗粒物测量需要 Green/IR 通道的自定义时序，具体序列需按 datasheet/固件补充。
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MAX30105_MODE_HR_ONLY = 0x02,
    MAX30105_MODE_SPO2 = 0x03,
    MAX30105_MODE_MULTI_LED = 0x07
} max30105_mode_t;

typedef enum {
    MAX30105_AVG_1 = 0,
    MAX30105_AVG_2,
    MAX30105_AVG_4,
    MAX30105_AVG_8,
    MAX30105_AVG_16,
    MAX30105_AVG_32
} max30105_sample_avg_t;

typedef struct {
    uint32_t red;
    uint32_t ir;
    uint32_t green;
} max30105_sample_t;

typedef struct {
    driver_i2c_t i2c;
    max30105_mode_t mode;
    uint8_t channels;
    bool initialized;
} max30105_handle_t;

/**
 * @brief  初始化 MAX30105：probe、软复位、配置 SpO2 模式
 * @param  i2c      已初始化的 I2C 主机句柄
 * @param  address  7 位地址，0 表示默认 0x57
 */
int max30105_init(max30105_handle_t *handle, hal_i2c_t *i2c, uint16_t address);

int max30105_deinit(max30105_handle_t *handle);

int max30105_reset(max30105_handle_t *handle);

/** 读取 FIFO 采样点；count 入参为 samples 容量，出参为实际读出个数 */
int max30105_read(max30105_handle_t *handle, max30105_sample_t *samples,
                  size_t capacity, size_t *count);

/** 通用寄存器写 / 读 */
int max30105_write(max30105_handle_t *handle, uint8_t reg, const uint8_t *data, size_t len);
int max30105_read_reg(max30105_handle_t *handle, uint8_t reg, uint8_t *data, size_t len);

int max30105_set_mode(max30105_handle_t *handle, max30105_mode_t mode);

int max30105_set_fifo(max30105_handle_t *handle, max30105_sample_avg_t average,
                      bool rollover, uint8_t almost_full);

/** 设置三路 LED 电流（寄存器原始值，1 LSB ≈ 0.2mA） */
int max30105_set_led_current(max30105_handle_t *handle, uint8_t red, uint8_t ir, uint8_t green);

/** 设置接近中断阈值（颗粒物检测用，原始值） */
int max30105_set_proximity_threshold(max30105_handle_t *handle, uint8_t threshold);

/** 触发并读取芯片温度（℃） */
int max30105_read_temperature(max30105_handle_t *handle, float *celsius);

int max30105_clear_fifo(max30105_handle_t *handle);

#ifdef __cplusplus
}
#endif

#endif /* MAX30105_H */
