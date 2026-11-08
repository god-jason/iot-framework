#ifndef SHT30_H
#define SHT30_H

/*
 * SHT30 / SHT31 / SHT35 温湿度传感器驱动（命令式 I2C）。
 *
 * 参考 libdriver 设计：
 *   sht30_init / sht30_deinit / sht30_read / sht30_soft_reset
 * 所有总线访问经由 hal 的 I2C 抽象。
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SHT30_REPEAT_HIGH = 0,
    SHT30_REPEAT_MEDIUM,
    SHT30_REPEAT_LOW
} sht30_repeatability_t;

typedef struct {
    driver_i2c_t i2c;
    sht30_repeatability_t repeatability;
    bool crc_enabled;
    bool initialized;
} sht30_handle_t;

typedef struct {
    float temperature;   /* ℃ */
    float humidity;      /* %RH */
} sht30_data_t;

/**
 * @brief  初始化 SHT30
 * @param  i2c      已初始化的 I2C 主机句柄
 * @param  address  7 位地址，通常 0x44
 */
int sht30_init(sht30_handle_t *handle, hal_i2c_t *i2c, uint16_t address);

int sht30_deinit(sht30_handle_t *handle);

/** 软复位 */
int sht30_soft_reset(sht30_handle_t *handle);

/** 使能/关闭片上加热器（用于除凝露自检） */
int sht30_set_heater(sht30_handle_t *handle, bool enable);

/** 单次测量并读取温湿度 */
int sht30_read(sht30_handle_t *handle, sht30_data_t *data);

/** 读取原始值（便于上层自行标定） */
int sht30_read_raw(sht30_handle_t *handle, uint16_t *raw_temperature, uint16_t *raw_humidity);

#ifdef __cplusplus
}
#endif

#endif /* SHT30_H */
