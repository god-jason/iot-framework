#ifndef QMC5883P_H
#define QMC5883P_H

/*
 * QMC5883P 三轴磁力计驱动（QST，I2C 7 位地址 0x2C）。
 *
 * 参考 libdriver 风格：
 *   qmc5883p_init / qmc5883p_deinit / qmc5883p_read / qmc5883p_read_raw
 * 所有硬件访问只经由 drivers/common/driver_io.h。
 *
 * 设计要点：
 *   - 0x00 为 Chip ID（0x80），init 强制校验；
 *   - 数据寄存器从 0x01 开始，三轴各 16 位小端；
 *   - 原始值按当前量程的 LSB/Gauss 换算成 µT（1 Gauss = 100 µT）；
 *   - heading 为 atan2(Y, X) 的 0~360° 结果，未做倾斜补偿与硬/软铁标定。
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 量程：枚举值同时作为 CTRL2.RNG[3:2] 编码 */
typedef enum {
    QMC5883P_RANGE_2G = 0,      /* ±2G， 15000 LSB/G */
    QMC5883P_RANGE_8G,          /* ±8G，  3750 LSB/G */
    QMC5883P_RANGE_12G,         /* ±12G， 2500 LSB/G */
    QMC5883P_RANGE_30G,         /* ±30G， 1000 LSB/G */
    QMC5883P_RANGE_COUNT
} qmc5883p_range_e;

typedef struct {
    float x;
    float y;
    float z;
} qmc5883p_vec3_t;

typedef struct {
    qmc5883p_vec3_t mag_ut;     /* 磁场，单位 µT */
    float heading_deg;          /* 航向角，0~360°，未做倾斜补偿 */
    bool data_ready;            /* STATUS.DRDY */
    bool overflow;              /* STATUS.OVL，量程溢出 */
} qmc5883p_data_t;

typedef struct {
    driver_i2c_t i2c;
    qmc5883p_range_e range;
    float ut_per_lsb;           /* 由量程换算得到 */
    uint8_t ctrl1;              /* 当前 CTRL1 影子值 */
    uint8_t ctrl2;              /* 当前 CTRL2 影子值 */
    bool initialized;
} qmc5883p_handle_t;

/**
 * @brief  初始化 QMC5883P（probe + 校验 Chip ID + 写配置并回读）
 * @param  handle   驱动句柄
 * @param  i2c      已初始化的 I2C 主机句柄
 * @param  address  7 位从机地址，0 表示默认 0x2C
 * @return 0 成功，负值为 HAL 状态码
 */
int qmc5883p_init(qmc5883p_handle_t *handle, hal_i2c_t *i2c, uint16_t address);

/** @brief 反初始化：进入 suspend 模式 */
int qmc5883p_deinit(qmc5883p_handle_t *handle);

/** @brief 读取磁场（µT）、航向角与状态位 */
int qmc5883p_read(qmc5883p_handle_t *handle, qmc5883p_data_t *data);

/** @brief 读取三轴原始计数 [x, y, z] */
int qmc5883p_read_raw(qmc5883p_handle_t *handle, int16_t xyz[3]);

/** @brief 读取 STATUS 寄存器（DRDY/OVL/DOR） */
int qmc5883p_get_status(qmc5883p_handle_t *handle, uint8_t *status);

/** @brief 切换量程，同时更新 µT/LSB 换算系数 */
int qmc5883p_set_range(qmc5883p_handle_t *handle, qmc5883p_range_e range);

/** @brief 软复位（CTRL2 软复位位）并重新下发配置 */
int qmc5883p_soft_reset(qmc5883p_handle_t *handle);

#ifdef __cplusplus
}
#endif

#endif /* QMC5883P_H */
