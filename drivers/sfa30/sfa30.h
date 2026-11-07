#ifndef SFA30_H
#define SFA30_H

/*
 * SFA30 甲醛传感器驱动（命令式 I2C，默认地址 0x5D）。
 *
 * 单位：hcho_ppb -> ppb（1 ppb = 0.001 ppm）；humidity -> %RH；temperature -> ℃。
 * 器件输出为整数 ticks，按 Sensirion 手册换算；温度缩放需按所用固件
 * datasheet 核对（见 sfa30_register.h）。
 *
 * init 中以“读序列号 + CRC 校验”作为 ID 校验。
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    driver_i2c_t i2c;
    bool initialized;
    bool measuring;
} sfa30_handle_t;

typedef struct {
    float hcho_ppb;
    float humidity;
    float temperature;
} sfa30_data_t;

/** @param address 7 位地址，传 0 取 0x5D */
int sfa30_init(sfa30_handle_t *handle, port_i2c_handle_t *i2c, uint16_t address);

int sfa30_deinit(sfa30_handle_t *handle);

/** 启动连续测量 */
int sfa30_start(sfa30_handle_t *handle);

int sfa30_stop(sfa30_handle_t *handle);

/** 读序列号（48 位），用于 ID 校验 */
int sfa30_get_serial(sfa30_handle_t *handle, uint64_t *serial);

/** 读取一次测量值（连续测量启动后约 1s 才有首个有效值） */
int sfa30_read(sfa30_handle_t *handle, sfa30_data_t *data);

#ifdef __cplusplus
}
#endif

#endif /* SFA30_H */
