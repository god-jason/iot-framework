#ifndef HMC5883L_H
#define HMC5883L_H

/*
 * HMC5883L 三轴磁力计驱动（I2C，7 位地址 0x1E）。
 *
 * 参考 libdriver 风格：
 *   hmc5883l_init / hmc5883l_deinit / hmc5883l_read / hmc5883l_read_raw
 *
 * 设计要点：
 *   - HMC5883L 没有 WHO_AM_I，init 校验 ID_A/ID_B/ID_C = 'H''4''3'，
 *     并回读 CFG_A/CFG_B 确认配置写入；
 *   - 数据寄存器字节序为 X(Z)Y：0x03 起依次 X_MSB/X_LSB/Z_MSB/Z_LSB/
 *     Y_MSB/Y_LSB，驱动按此顺序解析，切勿按 XYZ 直接读取；
 *   - 原始值按当前增益的 LSB/Gauss 换算为 µT（1 Gauss = 100 µT）；
 *   - read 检查 STATUS 的 RDY / LOCK 位；增益、平均次数、输出率可配置。
 *
 * 注意：驱动不做硬铁/软铁标定；航向角未做倾斜补偿。
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HMC5883L_AVG_1 = 0,
    HMC5883L_AVG_2,
    HMC5883L_AVG_4,
    HMC5883L_AVG_8,
    HMC5883L_AVG_COUNT
} hmc5883l_average_t;

typedef enum {
    HMC5883L_DR_0HZ75 = 0,
    HMC5883L_DR_1HZ5,
    HMC5883L_DR_3HZ,
    HMC5883L_DR_7HZ5,
    HMC5883L_DR_15HZ,
    HMC5883L_DR_30HZ,
    HMC5883L_DR_75HZ,
    HMC5883L_DR_COUNT
} hmc5883l_data_rate_t;

typedef enum {
    HMC5883L_GAIN_1_3GA = 0,    /* ±1.3 Ga, 1090 LSB/Ga */
    HMC5883L_GAIN_1_9GA,        /* ±1.9 Ga,  820 LSB/Ga */
    HMC5883L_GAIN_2_5GA,        /* ±2.5 Ga,  660 LSB/Ga */
    HMC5883L_GAIN_4_0GA,        /* ±4.0 Ga,  440 LSB/Ga */
    HMC5883L_GAIN_4_7GA,        /* ±4.7 Ga,  390 LSB/Ga */
    HMC5883L_GAIN_5_6GA,        /* ±5.6 Ga,  330 LSB/Ga */
    HMC5883L_GAIN_8_1GA,        /* ±8.1 Ga,  230 LSB/Ga */
    HMC5883L_GAIN_COUNT
} hmc5883l_gain_t;

typedef struct {
    float x;
    float y;
    float z;
} hmc5883l_vec3_t;

typedef struct {
    hmc5883l_vec3_t mag_ut;     /* 磁场，µT（X/Y/Z 已按正确字节序解析） */
    float heading_deg;          /* atan2(y, x) 转 0~360° */
    bool data_ready;            /* STATUS.RDY */
    bool locked;                /* STATUS.LOCK：数据寄存器正在更新 */
} hmc5883l_data_t;

typedef struct {
    driver_i2c_t i2c;
    hmc5883l_average_t average;
    hmc5883l_data_rate_t data_rate;
    hmc5883l_gain_t gain;
    float ut_per_lsb;           /* 由增益换算得到 */
    bool initialized;
} hmc5883l_handle_t;

/**
 * @brief  初始化 HMC5883L
 * @param  handle    驱动句柄
 * @param  i2c       已初始化的 I2C 主机句柄
 * @param  address   7 位从机地址，0 表示默认 0x1E
 * @param  gain      增益档位
 * @param  average   平均次数
 * @param  data_rate 输出率
 * @return 0 成功，负值为 port_status_t
 */
int hmc5883l_init(hmc5883l_handle_t *handle, port_i2c_handle_t *i2c, uint16_t address,
                  hmc5883l_gain_t gain, hmc5883l_average_t average,
                  hmc5883l_data_rate_t data_rate);

/** @brief 反初始化：进入空闲模式 */
int hmc5883l_deinit(hmc5883l_handle_t *handle);

/** @brief 读取磁场（µT）、航向角与状态位 */
int hmc5883l_read(hmc5883l_handle_t *handle, hmc5883l_data_t *data);

/** @brief 读取原始计数 [x, y, z]（大端，已按 X/Z/Y 字节序重排） */
int hmc5883l_read_raw(hmc5883l_handle_t *handle, int16_t xyz[3]);

/** @brief 读取 STATUS 寄存器 */
int hmc5883l_get_status(hmc5883l_handle_t *handle, uint8_t *status);

/** @brief 修改增益（同时更新灵敏度换算系数） */
int hmc5883l_set_gain(hmc5883l_handle_t *handle, hmc5883l_gain_t gain);

#ifdef __cplusplus
}
#endif

#endif /* HMC5883L_H */
