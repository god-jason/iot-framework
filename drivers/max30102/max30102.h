#ifndef MAX30102_H
#define MAX30102_H

/*
 * MAX30102 心率 / 血氧传感器驱动（I2C，FIFO 读取）。
 *
 * 参考 libdriver 风格：
 *   max30102_init / max30102_deinit / max30102_read / max30102_write
 * 每个 FIFO 采样点包含 RED、IR 两个通道，各 18 位（左对齐存 3 字节）。
 * 心率/血氧算法由上层实现，本驱动只负责配置与原始数据搬运。
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MAX30102_MODE_HR_ONLY = 0x02,
    MAX30102_MODE_SPO2 = 0x03,
    MAX30102_MODE_MULTI_LED = 0x07
} max30102_mode_t;

typedef enum {
    MAX30102_AVG_1 = 0,
    MAX30102_AVG_2,
    MAX30102_AVG_4,
    MAX30102_AVG_8,
    MAX30102_AVG_16,
    MAX30102_AVG_32
} max30102_sample_avg_t;

typedef struct {
    uint32_t red;
    uint32_t ir;
} max30102_sample_t;

typedef struct {
    driver_i2c_t i2c;
    max30102_mode_t mode;
    uint8_t channels;      /* 每个采样点的通道数 */
    bool initialized;
} max30102_handle_t;

/**
 * @brief  初始化 MAX30102：probe、软复位、配置 SpO2 模式
 * @param  i2c      已初始化的 I2C 主机句柄
 * @param  address  7 位地址，0 表示默认 0x57
 */
int max30102_init(max30102_handle_t *handle, hal_i2c_t *i2c, uint16_t address);

int max30102_deinit(max30102_handle_t *handle);

/** 软复位并等待完成 */
int max30102_reset(max30102_handle_t *handle);

/** 读取 FIFO 中的采样点；count 入参为 samples 容量，出参为实际读出个数（可为 0） */
int max30102_read(max30102_handle_t *handle, max30102_sample_t *samples,
                  size_t capacity, size_t *count);

/** 通用寄存器写 */
int max30102_write(max30102_handle_t *handle, uint8_t reg, const uint8_t *data, size_t len);

/** 通用寄存器读 */
int max30102_read_reg(max30102_handle_t *handle, uint8_t reg, uint8_t *data, size_t len);

/** 设置工作模式 */
int max30102_set_mode(max30102_handle_t *handle, max30102_mode_t mode);

/** 设置 FIFO 平均次数、回绕与 almost full 阈值（0..15） */
int max30102_set_fifo(max30102_handle_t *handle, max30102_sample_avg_t average,
                      bool rollover, uint8_t almost_full);

/** 设置 LED 驱动电流（寄存器原始值，1 LSB ≈ 0.2mA） */
int max30102_set_led_current(max30102_handle_t *handle, uint8_t red, uint8_t ir);

/** 触发并读取芯片温度（℃） */
int max30102_read_temperature(max30102_handle_t *handle, float *celsius);

/** 清空 FIFO 指针 */
int max30102_clear_fifo(max30102_handle_t *handle);

#ifdef __cplusplus
}
#endif

#endif /* MAX30102_H */
