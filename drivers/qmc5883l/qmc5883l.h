#ifndef QMC5883L_H
#define QMC5883L_H

/*
 * QMC5883L 三轴磁力计驱动（QST，I2C 7 位地址 0x0D）。
 *
 * 参考 libdriver 风格：
 *   qmc5883l_init / qmc5883l_deinit / qmc5883l_read / qmc5883l_read_raw
 * 所有硬件访问只经由 drivers/common/driver_io.h。
 *
 * 设计要点：
 *   - 数据寄存器为 16 位小端，顺序 X/Y/Z（0x00~0x05）；
 *   - 原始值按当前量程的 LSB/Gauss 换算成 µT（1 Gauss = 100 µT）；
 *   - heading 为 atan2(Y, X) 的 0~360° 结果，未做倾斜补偿与硬/软铁标定；
 *   - read 前检查 STATUS.DRDY，OVL 置位表示溢出（数据不可信）。
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 量程：枚举值同时作为 CTRL1.RNG[5:4] 编码 */
typedef enum {
    QMC5883L_RANGE_2G = 0,      /* ±2G，12000 LSB/G */
    QMC5883L_RANGE_8G,          /* ±8G， 3000 LSB/G */
    QMC5883L_RANGE_COUNT
} qmc5883l_range_t;

typedef struct {
    float x;
    float y;
    float z;
} qmc5883l_vec3_t;

typedef struct {
    qmc5883l_vec3_t mag_ut;     /* 磁场，单位 µT */
    float heading_deg;          /* 航向角，0~360°，未做倾斜补偿 */
    bool data_ready;            /* STATUS.DRDY */
    bool overflow;              /* STATUS.OVL，量程溢出 */
} qmc5883l_data_t;

typedef struct {
    driver_i2c_t i2c;
    qmc5883l_range_t range;
    float ut_per_lsb;           /* 由量程换算得到 */
    uint8_t ctrl1;              /* 当前 CTRL1 影子值 */
    bool initialized;
} qmc5883l_handle_t;

/**
 * @brief  初始化 QMC5883L（probe + 软复位 + 写配置并回读校验）
 * @param  handle   驱动句柄
 * @param  i2c      已初始化的 I2C 主机句柄
 * @param  address  7 位从机地址，0 表示默认 0x0D
 * @return 0 成功，负值为 port_status_t
 */
int qmc5883l_init(qmc5883l_handle_t *handle, port_i2c_handle_t *i2c, uint16_t address);

/** @brief 反初始化：进入待机模式 */
int qmc5883l_deinit(qmc5883l_handle_t *handle);

/** @brief 读取磁场（µT）、航向角与状态位 */
int qmc5883l_read(qmc5883l_handle_t *handle, qmc5883l_data_t *data);

/** @brief 读取三轴原始计数 [x, y, z] */
int qmc5883l_read_raw(qmc5883l_handle_t *handle, int16_t xyz[3]);

/** @brief 读取 STATUS 寄存器（DRDY/OVL/DOR） */
int qmc5883l_get_status(qmc5883l_handle_t *handle, uint8_t *status);

/** @brief 切换量程，同时更新 µT/LSB 换算系数 */
int qmc5883l_set_range(qmc5883l_handle_t *handle, qmc5883l_range_t range);

/** @brief 软复位并重新下发配置（配置内容取自句柄） */
int qmc5883l_soft_reset(qmc5883l_handle_t *handle);

#ifdef __cplusplus
}
#endif

#endif /* QMC5883L_H */
