#ifndef PCA9685_H
#define PCA9685_H

/*
 * PCA9685 16 通道 12 位 PWM 控制器驱动（I2C）。
 *
 * 每个通道有 ON/OFF 两个 12 位计数值，周期 4096 计。
 * 参考 libdriver 风格：pca9685_init / pca9685_set_pwm / pca9685_set_pwm_freq。
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    driver_i2c_t i2c;
    float frequency;        /* 当前 PWM 频率（Hz） */
    bool initialized;
} pca9685_handle_t;

/**
 * @brief  初始化：探测器件、唤醒并打开地址自动递增
 * @param  handle   驱动句柄
 * @param  i2c      已初始化的 I2C 主机句柄
 * @param  address  7 位地址（0x40~0x7F），传 0 使用 0x40
 * @return 0 成功，负值为 port_status_t
 * @note   init 不改变 prescale；默认频率为芯片复位值（约 200Hz），
 *         需要其他频率请调用 pca9685_set_pwm_freq。
 */
int pca9685_init(pca9685_handle_t *handle, port_i2c_handle_t *i2c, uint16_t address);

/** @brief 反初始化：所有通道全关，不关闭总线 */
int pca9685_deinit(pca9685_handle_t *handle);

/**
 * @brief  设置 PWM 频率（24Hz~1526Hz）
 * @param  freq_hz  目标频率
 * @note   内部会进入 SLEEP、写 PRESCALE、唤醒并等待 1ms
 */
int pca9685_set_pwm_freq(pca9685_handle_t *handle, float freq_hz);

/**
 * @brief  设置通道的 ON/OFF 计数值
 * @param  channel  0~15
 * @param  on       0~4095
 * @param  off      0~4095（4096 由通道全开位实现，请用 pca9685_set_full_on）
 */
int pca9685_set_pwm(pca9685_handle_t *handle, uint8_t channel, uint16_t on, uint16_t off);

/**
 * @brief  按占空比设置通道
 * @param  duty  0.0~1.0（1.0 输出恒高）
 */
int pca9685_set_duty(pca9685_handle_t *handle, uint8_t channel, float duty);

/** @brief 设置所有通道的 ON/OFF 计数值 */
int pca9685_set_all(pca9685_handle_t *handle, uint16_t on, uint16_t off);

/** @brief 通道全开/全关（写 OFF_H 的全关位） */
int pca9685_set_full_on(pca9685_handle_t *handle, uint8_t channel, bool full_on);

/** @brief 全部通道使能/关闭输出 */
int pca9685_enable_output(pca9685_handle_t *handle, bool enable);

#ifdef __cplusplus
}
#endif

#endif /* PCA9685_H */
