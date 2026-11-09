#ifndef ADXL362_H
#define ADXL362_H

/*
 * ADXL362 低功耗三轴加速度计驱动（仅 SPI，4 线）。
 *
 * 参考 libdriver 风格：
 *   adxl362_init / adxl362_deinit / adxl362_read / adxl362_read_raw
 *
 * 设计要点：
 *   - ADXL362 没有 WHO_AM_I，init 通过 DEVID_AD(0xAD) / DEVID_MST(0x1D) /
 *     PARTID(0xF2) 三个身份寄存器完成在位校验；
 *   - 12 位输出，灵敏度固定 1 mg/LSB，read 输出 g；
 *   - read_raw 输出 12 位有符号计数；
 *   - 量程（±2/4/8g）与输出率（12.5~400Hz）可配置；
 *   - STATUS 的 OVR / ERR_USER_REGS 会被检查。
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ADXL362_RANGE_2G = 0,
    ADXL362_RANGE_4G,
    ADXL362_RANGE_8G,
    ADXL362_RANGE_COUNT
} adxl362_range_e;

/* 枚举值与 FILTER_CTL 中的 ODR 编码一致（1~6） */
typedef enum {
    ADXL362_ODR_12HZ5 = 0x01,
    ADXL362_ODR_25HZ = 0x02,
    ADXL362_ODR_50HZ = 0x03,
    ADXL362_ODR_100HZ = 0x04,
    ADXL362_ODR_200HZ = 0x05,
    ADXL362_ODR_400HZ = 0x06
} adxl362_odr_e;

typedef struct {
    float x;
    float y;
    float z;
} adxl362_vec3_t;

typedef struct {
    adxl362_vec3_t accel_g;     /* 加速度，g */
    float temperature_c;        /* 片上温度，°C */
    bool data_ready;            /* STATUS.DATA_RDY */
    bool overflow;              /* STATUS.OVR：FIFO/数据溢出 */
} adxl362_data_t;

typedef struct {
    driver_spi_t spi;
    adxl362_range_e range;
    adxl362_odr_e odr;
    bool initialized;
} adxl362_handle_t;

/**
 * @brief  初始化 ADXL362
 * @param  handle  驱动句柄
 * @param  spi     已初始化的 SPI 主机句柄
 * @param  cs      片选引脚，总线自带片选时传 0
 * @param  range   量程
 * @param  odr     输出率
 * @return 0 成功，负值为 HAL 状态码
 */
int adxl362_init(adxl362_handle_t *handle, hal_spi_t *spi, hal_pin_t cs,
                 adxl362_range_e range, adxl362_odr_e odr);

/** @brief 反初始化：进入待机模式 */
int adxl362_deinit(adxl362_handle_t *handle);

/** @brief 读取加速度（g）与温度，附带状态位 */
int adxl362_read(adxl362_handle_t *handle, adxl362_data_t *data);

/**
 * @brief 读取原始计数
 * @param  xyz           12 位有符号计数 [x, y, z]
 * @param  temperature_raw 12 位有符号温度计数，可为 NULL
 */
int adxl362_read_raw(adxl362_handle_t *handle, int16_t xyz[3], int16_t *temperature_raw);

/** @brief 读取 STATUS 寄存器 */
int adxl362_get_status(adxl362_handle_t *handle, uint8_t *status);

#ifdef __cplusplus
}
#endif

#endif /* ADXL362_H */
