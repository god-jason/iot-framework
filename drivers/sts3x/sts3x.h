#ifndef STS3X_H
#define STS3X_H

/*
 * Sensirion STS3x 高精度温度传感器驱动（I2C，命令式）。
 *
 * 参考 libdriver 风格：
 *   sts3x_init / sts3x_deinit / sts3x_read / sts3x_set_repeatability
 * 换算：T(℃) = -45 + 175 * raw / 65535
 * 每个数据字后跟 1 字节 CRC（poly 0x31, init 0xFF）。
 */

#include "common/driver_io.h"
#include "sts3x_register.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    STS3X_REPEAT_HIGH = 0,
    STS3X_REPEAT_MEDIUM,
    STS3X_REPEAT_LOW
} sts3x_repeatability_t;

typedef struct {
    driver_i2c_t i2c;
    sts3x_repeatability_t repeatability;
    bool crc_enabled;
    bool initialized;
} sts3x_handle_t;

/**
 * @brief  初始化 STS3x
 * @param  i2c      已初始化的 I2C 主机句柄
 * @param  address  7 位地址，0 表示默认 0x4A
 */
int sts3x_init(sts3x_handle_t *handle, iot_i2c_t *i2c, uint16_t address);

int sts3x_deinit(sts3x_handle_t *handle);

/** 单次测量，返回摄氏度 */
int sts3x_read(sts3x_handle_t *handle, float *celsius);

/** 单次测量，返回原始 16 位值 */
int sts3x_read_raw(sts3x_handle_t *handle, uint16_t *raw);

/** 设置重复性（决定测量时间与噪声） */
int sts3x_set_repeatability(sts3x_handle_t *handle, sts3x_repeatability_t repeatability);

/** 使能/关闭 CRC 校验 */
int sts3x_set_crc(sts3x_handle_t *handle, bool enable);

/** 软复位 */
int sts3x_soft_reset(sts3x_handle_t *handle);

/** 读取状态寄存器（bit0 = 最近一次写命令 CRC 错误） */
int sts3x_read_status(sts3x_handle_t *handle, uint16_t *status);

#ifdef __cplusplus
}
#endif

#endif /* STS3X_H */
