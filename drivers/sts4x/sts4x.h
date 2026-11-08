#ifndef STS4X_H
#define STS4X_H

/*
 * Sensirion STS4x（STS40 系列）第 4 代温度传感器驱动（I2C，命令式）。
 *
 * 参考 libdriver 风格：
 *   sts4x_init / sts4x_deinit / sts4x_read / sts4x_set_repeatability
 * 换算：T(℃) = -45 + 175 * raw / 65535
 * 单次测量返回 2 字节数据 + 1 字节 CRC（poly 0x31, init 0xFF）。
 */

#include "common/driver_io.h"
#include "sts4x_register.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    STS4X_REPEAT_HIGH = 0,
    STS4X_REPEAT_MEDIUM,
    STS4X_REPEAT_LOW
} sts4x_repeatability_t;

typedef struct {
    driver_i2c_t i2c;
    sts4x_repeatability_t repeatability;
    bool crc_enabled;
    bool initialized;
} sts4x_handle_t;

/**
 * @brief  初始化 STS4x
 * @param  i2c      已初始化的 I2C 主机句柄
 * @param  address  7 位地址，0 表示默认 0x46
 */
int sts4x_init(sts4x_handle_t *handle, hal_i2c_t *i2c, uint16_t address);

int sts4x_deinit(sts4x_handle_t *handle);

/** 单次测量，返回摄氏度 */
int sts4x_read(sts4x_handle_t *handle, float *celsius);

/** 单次测量，返回原始 16 位值 */
int sts4x_read_raw(sts4x_handle_t *handle, uint16_t *raw);

int sts4x_set_repeatability(sts4x_handle_t *handle, sts4x_repeatability_t repeatability);

int sts4x_set_crc(sts4x_handle_t *handle, bool enable);

int sts4x_soft_reset(sts4x_handle_t *handle);

/** 读取 48 位序列号（6 字节，含 CRC 校验） */
int sts4x_read_serial(sts4x_handle_t *handle, uint8_t serial[6]);

#ifdef __cplusplus
}
#endif

#endif /* STS4X_H */
