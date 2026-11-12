#ifndef AS5600_H
#define AS5600_H

/*
 * AS5600 磁性旋转编码器驱动（I2C，7 位地址 0x36）。
 *
 * 参考 libdriver 风格：
 *   as5600_init / as5600_deinit / as5600_read / as5600_read_raw
 *
 * 设计要点：
 *   - AS5600 没有 WHO_AM_I：init 通过 ACK 探测 + CONF 寄存器写入/回读
 *     + STATUS 可读性完成在位与写通路自检；
 *   - 12 位角度（0~4095）换算为 0~360°，read 同时给出原始值与滤波值；
 *   - read 检查 STATUS 的 MD/MH/ML 磁铁状态位；
 *   - 电源模式、迟滞、慢滤波均可配置。
 *
 * 注意：单圈绝对角度，跨零点（4095→0）不做多圈累加，需要多圈的调用方
 *       自行按角度跳变计数。
 */

#include "hal.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AS5600_POWER_MODE_NOMINAL = 0,  /* 正常模式，最低噪声 */
    AS5600_POWER_MODE_LPM1,         /* 低功耗 1 */
    AS5600_POWER_MODE_LPM2,         /* 低功耗 2 */
    AS5600_POWER_MODE_LPM3          /* 低功耗 3 */
} as5600_power_mode_e;

typedef struct {
    float angle_deg;                /* 滤波后角度，0~360° */
    uint16_t raw_angle;             /* 未滤波 12 位计数 0~4095 */
    uint16_t angle;                 /* 滤波后 12 位计数 0~4095 */
    bool magnet_detected;           /* STATUS.MD */
    bool magnet_too_strong;         /* STATUS.MH */
    bool magnet_too_weak;           /* STATUS.ML */
} as5600_data_t;

typedef struct {
    hal_i2c_dev_t i2c;
    as5600_power_mode_e power_mode;
    uint16_t conf;                  /* 最近一次读取的 CONF 值 */
    bool initialized;
} as5600_handle_t;

/**
 * @brief  初始化 AS5600
 * @param  handle  驱动句柄
 * @param  i2c     已初始化的 I2C 主机句柄
 * @param  address 7 位从机地址，0 表示默认 0x36
 * @return 0 成功，负值为 HAL 状态码
 */
int as5600_init(as5600_handle_t *handle, hal_i2c_t *i2c, uint16_t address);

/** @brief 反初始化（不关闭总线） */
int as5600_deinit(as5600_handle_t *handle);

/** @brief 读取角度（度）、原始计数与磁铁状态 */
int as5600_read(as5600_handle_t *handle, as5600_data_t *data);

/**
 * @brief 读取原始角度计数
 * @param  raw_angle 0~4095（未滤波 RAW_ANGLE）
 */
int as5600_read_raw(as5600_handle_t *handle, uint16_t *raw_angle);

/** @brief 读取 STATUS 寄存器（MD/MH/ML） */
int as5600_get_status(as5600_handle_t *handle, uint8_t *status);

/** @brief 设置电源模式（NOM/LPM1~LPM3） */
int as5600_set_power_mode(as5600_handle_t *handle, as5600_power_mode_e mode);

/** @brief 设置输出迟滞（0~3 LSB） */
int as5600_set_hysteresis(as5600_handle_t *handle, uint8_t lsb);

/** @brief 设置慢滤波（0:16x, 1:8x, 2:4x, 3:2x） */
int as5600_set_slow_filter(as5600_handle_t *handle, uint8_t filter);

#ifdef __cplusplus
}
#endif

#endif /* AS5600_H */
