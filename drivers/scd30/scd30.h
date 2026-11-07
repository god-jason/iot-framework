#ifndef SCD30_H
#define SCD30_H

/*
 * SCD30 CO2 / 温度 / 湿度传感器驱动（命令式 I2C，默认地址 0x61）。
 *
 * 单位：co2 -> ppm；temperature -> ℃；humidity -> %RH。
 * 数据由器件以 IEEE754 大端 float 给出（3 个字，每字带 CRC）。
 *
 * init 中以“读固件版本 + CRC 校验”作为器件存在性/ID 校验（SCD30 无 ID 寄存器）。
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    driver_i2c_t i2c;
    uint16_t interval_s;
    bool initialized;
} scd30_handle_t;

typedef struct {
    float co2_ppm;
    float temperature;
    float humidity;
} scd30_data_t;

/**
 * @brief  初始化并启动连续测量
 * @param  address 7 位地址，传 0 取 0x61
 */
int scd30_init(scd30_handle_t *handle, port_i2c_handle_t *i2c, uint16_t address);

int scd30_deinit(scd30_handle_t *handle);

/** 停止连续测量 */
int scd30_stop(scd30_handle_t *handle);

/** 设置测量间隔（2..1800 s），内部重启连续测量 */
int scd30_set_interval(scd30_handle_t *handle, uint16_t interval_s);

/** 温度偏移，单位 0.01℃ */
int scd30_set_temperature_offset(scd30_handle_t *handle, uint16_t offset_centi_c);

/** 海拔补偿，单位 m */
int scd30_set_altitude(scd30_handle_t *handle, uint16_t altitude_m);

/** 强制基线校准，reference_ppm 为已知 CO2 浓度 */
int scd30_set_forced_recalibration(scd30_handle_t *handle, uint16_t reference_ppm);

/** 读固件版本（ID 校验用） */
int scd30_get_firmware_version(scd30_handle_t *handle, uint16_t *version);

/**
 * @brief  读取一次测量结果（数据未就绪返回 PORT_NOT_READY）
 * @note   连续测量模式下器件每 interval_s 更新一次；调用方可用
 *         driver_tick_ms() 自行控制轮询节拍。
 */
int scd30_read(scd30_handle_t *handle, scd30_data_t *data);

#ifdef __cplusplus
}
#endif

#endif /* SCD30_H */
