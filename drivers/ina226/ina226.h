#ifndef INA226_H
#define INA226_H

/*
 * INA226 电流 / 电压 / 功率监测器驱动（I2C）。
 *
 * 参考 libdriver 风格：ina226_init / ina226_deinit / ina226_read /
 * ina226_read_raw / ina226_set_calibration。
 *
 * 设计要点：
 *   - init 校验 DIE_ID(0xFF)=0x2260 与厂商 ID(0xFE)=0x5449，写配置寄存器，
 *     再按默认分流参数（0.1Ω / 3.2A）写入校准寄存器；
 *   - 分流电阻与最大电流可用 ina226_set_calibration() 重新配置，驱动按
 *     CAL = 0.00512 / (current_lsb * Rshunt) 重新计算并写入校准寄存器；
 *   - 总线电压 1 LSB = 1.25 mV 且不移位（旧实现误用 INA219 的 >>3 已成历史）；
 *   - read 输出 V / A / W 物理量，read_raw 输出各寄存器原始 16 位值。
 *
 * 用法：
 *   ina226_handle_t mon;
 *   ina226_init(&mon, &i2c1, INA226_DEFAULT_ADDRESS);
 *   ina226_set_calibration(&mon, 0.002f, 10.0f);   // 2mΩ 分流、10A 满量程
 *   ina226_data_t d;
 *   ina226_read(&mon, &d);   // bus_voltage(V) / current(A) / power(W)
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    driver_i2c_t i2c;
    float shunt_ohm;         /* 分流电阻，单位 Ω */
    float max_current_a;     /* 预期最大电流，单位 A */
    float current_lsb_a;     /* 电流寄存器 1 LSB 对应的安培数 */
    float power_lsb_w;       /* 功率寄存器 1 LSB 对应的瓦特数（25 * current_lsb） */
    uint16_t calibration;    /* 已写入的校准寄存器值 */
    bool initialized;
} ina226_handle_t;

typedef struct {
    float bus_voltage;       /* 总线电压，单位 V */
    float shunt_voltage;     /* 分流电压，单位 V */
    float current;           /* 电流，单位 A */
    float power;             /* 功率，单位 W */
} ina226_data_t;

/**
 * @brief  初始化 INA226
 * @param  handle   驱动句柄
 * @param  i2c      已初始化的 I2C 主机句柄
 * @param  address  7 位从机地址，0 表示使用默认 0x40
 * @return 0 成功，负值为 HAL 状态码（-11 表示 ID 不匹配）
 */
int ina226_init(ina226_handle_t *handle, hal_i2c_t *i2c, uint16_t address);

/** @brief 反初始化：器件进入掉电模式，复位句柄状态（不关闭总线） */
int ina226_deinit(ina226_handle_t *handle);

/**
 * @brief  配置分流电阻与最大电流，并重新计算 / 写入校准寄存器
 * @param  shunt_ohm      分流电阻，单位 Ω，必须 > 0
 * @param  max_current_a  预期最大电流，单位 A，必须 > 0
 * @note   电流 LSB = max_current_a / 32768；校准值超出 1~65535 范围时按边界截断，
 *         此时实际满量程会与请求值不同（可回读 handle->calibration 判断）。
 */
int ina226_set_calibration(ina226_handle_t *handle, float shunt_ohm, float max_current_a);

/** @brief 读取物理量：总线电压 V、分流电压 V、电流 A、功率 W */
int ina226_read(ina226_handle_t *handle, ina226_data_t *data);

/**
 * @brief  读取原始寄存器值
 * @param  shunt_raw    分流电压原始值（有符号），可为 NULL
 * @param  bus_raw      总线电压原始值（无符号，1 LSB = 1.25mV），可为 NULL
 * @param  current_raw  电流原始值（有符号），可为 NULL
 * @param  power_raw    功率原始值（无符号），可为 NULL
 */
int ina226_read_raw(ina226_handle_t *handle, int16_t *shunt_raw, uint16_t *bus_raw,
                    int16_t *current_raw, uint16_t *power_raw);

/** @brief 读取总线电压，单位 V */
int ina226_read_bus_voltage(ina226_handle_t *handle, float *voltage);

/** @brief 读取分流电压，单位 V */
int ina226_read_shunt_voltage(ina226_handle_t *handle, float *voltage);

/** @brief 读取电流，单位 A */
int ina226_read_current(ina226_handle_t *handle, float *current);

/** @brief 读取功率，单位 W */
int ina226_read_power(ina226_handle_t *handle, float *power);

#ifdef __cplusplus
}
#endif

#endif /* INA226_H */
