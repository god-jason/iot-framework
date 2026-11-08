#ifndef PCF8591_H
#define PCF8591_H

/*
 * PCF8591 8 位 ADC/DAC 驱动（I2C）。
 *
 * 4 路单端 / 3 路差分 / 混合模式输入，1 路 8 位电压输出。
 * 参考 libdriver 风格：pcf8591_init / pcf8591_read / pcf8591_write。
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 模拟输入模式（对应控制字节 bit5:4） */
typedef enum {
    PCF8591_MODE_4_SINGLE_ENDED = 0,   /* 4 路单端 */
    PCF8591_MODE_3_DIFFERENTIAL,       /* 3 路差分 */
    PCF8591_MODE_MIXED,                /* 2 单端 + 1 差分 */
    PCF8591_MODE_2_DIFFERENTIAL        /* 2 路差分 */
} pcf8591_input_mode_t;

typedef struct {
    driver_i2c_t i2c;
    pcf8591_input_mode_t input_mode;
    uint8_t channel;            /* 当前通道 0~3 */
    bool auto_increment;        /* 读操作后通道自动递增 */
    bool dac_enabled;           /* 是否使能模拟输出 */
    uint16_t vref_mv;           /* 参考电压，用于电压换算 */
    bool initialized;
} pcf8591_handle_t;

/**
 * @brief  初始化：探测器件并写入默认控制字节
 * @param  handle   驱动句柄
 * @param  i2c      已初始化的 I2C 主机句柄
 * @param  address  7 位地址（0x48~0x4F），传 0 使用 0x48
 * @return 0 成功，负值为 hal_status_t
 */
int pcf8591_init(pcf8591_handle_t *handle, hal_i2c_t *i2c, uint16_t address);

/** @brief 反初始化（不关闭总线） */
int pcf8591_deinit(pcf8591_handle_t *handle);

/** @brief 设置输入模式（写控制字节） */
int pcf8591_set_input_mode(pcf8591_handle_t *handle, pcf8591_input_mode_t mode);

/** @brief 设置当前通道（0~3） */
int pcf8591_set_channel(pcf8591_handle_t *handle, uint8_t channel);

/** @brief 使能/禁用通道自动递增 */
int pcf8591_set_auto_increment(pcf8591_handle_t *handle, bool enable);

/** @brief 使能/禁用模拟输出（AOUT） */
int pcf8591_set_dac_enabled(pcf8591_handle_t *handle, bool enable);

/** @brief 设置参考电压（mV），用于电压换算 */
int pcf8591_set_vref(pcf8591_handle_t *handle, uint16_t vref_mv);

/**
 * @brief  读取指定通道的 8 位 ADC 结果
 * @param  channel  0~3
 * @param  value    0~255
 */
int pcf8591_read(pcf8591_handle_t *handle, uint8_t channel, uint8_t *value);

/** @brief 读取指定通道的电压（V） */
int pcf8591_read_voltage(pcf8591_handle_t *handle, uint8_t channel, float *volts);

/**
 * @brief  写 DAC 输出（8 位）
 * @param  value  0~255，输出约 value/256 × Vref
 */
int pcf8591_write(pcf8591_handle_t *handle, uint8_t value);

#ifdef __cplusplus
}
#endif

#endif /* PCF8591_H */
