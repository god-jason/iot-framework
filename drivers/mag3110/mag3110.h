#ifndef MAG3110_H
#define MAG3110_H

/*
 * MAG3110 三轴磁力计驱动（I2C，7 位地址 0x0E）。
 *
 * 参考 libdriver 风格：
 *   mag3110_init / mag3110_deinit / mag3110_read / mag3110_read_raw
 *
 * 设计要点：
 *   - init 校验 WHO_AM_I(0x07，期望 0xC4)，CTRL 寄存器回读校验；
 *   - 原始 16 位数据按 0.1 µT/LSB 换算为 µT，并给出 X-Y 航向角；
 *   - read 前检查 DR_STATUS 的 ZYXDR 与溢出位；
 *   - 数据率（10~80Hz）与过采样倍数可配置。
 *
 * 注意：驱动不包含硬铁/软铁标定，原始数据含环境偏置；航向角未做倾斜补偿。
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MAG3110_DR_80HZ = 0,
    MAG3110_DR_40HZ,
    MAG3110_DR_20HZ,
    MAG3110_DR_10HZ,
    MAG3110_DR_COUNT
} mag3110_data_rate_e;

typedef enum {
    MAG3110_OS_16 = 0,      /* 16 倍过采样 */
    MAG3110_OS_32,
    MAG3110_OS_64,
    MAG3110_OS_128,
    MAG3110_OS_COUNT
} mag3110_oversampling_e;

typedef struct {
    float x;
    float y;
    float z;
} mag3110_vec3_t;

typedef struct {
    mag3110_vec3_t mag_ut;      /* 磁场，µT */
    float heading_deg;          /* atan2(y, x) 转 0~360° */
    float die_temp_c;           /* 片上温度，°C（仅参考） */
} mag3110_data_t;

typedef struct {
    driver_i2c_t i2c;
    mag3110_data_rate_e data_rate;
    mag3110_oversampling_e oversampling;
    bool initialized;
} mag3110_handle_t;

/**
 * @brief  初始化 MAG3110
 * @param  handle       驱动句柄
 * @param  i2c          已初始化的 I2C 主机句柄
 * @param  address      7 位从机地址，0 表示默认 0x0E
 * @param  data_rate    数据率
 * @param  oversampling 过采样倍数
 * @return 0 成功，负值为 HAL 状态码
 */
int mag3110_init(mag3110_handle_t *handle, hal_i2c_t *i2c, uint16_t address,
                 mag3110_data_rate_e data_rate, mag3110_oversampling_e oversampling);

/** @brief 反初始化：进入待机模式 */
int mag3110_deinit(mag3110_handle_t *handle);

/** @brief 读取磁场（µT）、航向角与片上温度 */
int mag3110_read(mag3110_handle_t *handle, mag3110_data_t *data);

/** @brief 读取原始计数 [x, y, z]（16 位补码，大端） */
int mag3110_read_raw(mag3110_handle_t *handle, int16_t xyz[3]);

/** @brief 读取 DR_STATUS（数据就绪 / 溢出） */
int mag3110_get_status(mag3110_handle_t *handle, uint8_t *status);

/** @brief 修改数据率 */
int mag3110_set_data_rate(mag3110_handle_t *handle, mag3110_data_rate_e data_rate);

#ifdef __cplusplus
}
#endif

#endif /* MAG3110_H */
