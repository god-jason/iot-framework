#ifndef SCD4X_H
#define SCD4X_H

/*
 * SCD40 / SCD41 CO2 / 温度 / 湿度传感器驱动（命令式 I2C，默认地址 0x62）。
 *
 * 单位：co2_ppm -> ppm；temperature -> ℃；humidity -> %RH。
 * 连续测量模式下器件每 5s 更新一次数据。
 *
 * init 中读取序列号（带 CRC 校验）作为 ID 校验。
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    driver_i2c_t i2c;
    bool initialized;
} scd4x_handle_t;

typedef struct {
    uint16_t co2_ppm;
    float temperature;
    float humidity;
} scd4x_data_t;

/** @param address 7 位地址，传 0 取 0x62 */
int scd4x_init(scd4x_handle_t *handle, iot_i2c_t *i2c, uint16_t address);

int scd4x_deinit(scd4x_handle_t *handle);

/** 读序列号（48 位） */
int scd4x_get_serial(scd4x_handle_t *handle, uint64_t *serial);

/** 停止周期测量 */
int scd4x_stop(scd4x_handle_t *handle);

/** 重新初始化（从低功耗/复位状态恢复） */
int scd4x_reinit(scd4x_handle_t *handle);

/** 进入低功耗，之后需 scd4x_reinit 或重新上电 */
int scd4x_power_down(scd4x_handle_t *handle);

/** 温度偏移，单位 0.01℃ */
int scd4x_set_temperature_offset(scd4x_handle_t *handle, uint16_t offset_centi_c);

/** 海拔补偿，单位 m */
int scd4x_set_altitude(scd4x_handle_t *handle, uint16_t altitude_m);

/** 强制基线校准，reference_ppm 为已知 CO2 浓度 */
int scd4x_force_recalibration(scd4x_handle_t *handle, uint16_t reference_ppm);

/** 数据未就绪返回 IOT_NOT_READY */
int scd4x_read(scd4x_handle_t *handle, scd4x_data_t *data);

#ifdef __cplusplus
}
#endif

#endif /* SCD4X_H */
