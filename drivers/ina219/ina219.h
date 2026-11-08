#ifndef INA219_H
#define INA219_H

/*
 * INA219 电流 / 电压 / 功率监测驱动（I2C）。
 *
 * 初始化时根据分流电阻与最大电流计算校准寄存器，
 * 之后可直接读取总线电压、电流与功率。
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    driver_i2c_t i2c;
    float shunt_ohm;
    float current_lsb_a;
    float power_lsb_w;
    uint16_t calibration;
    bool initialized;
} ina219_handle_t;

typedef struct {
    float bus_voltage;   /* V */
    float current;       /* A */
    float power;         /* W */
} ina219_data_t;

int ina219_init(ina219_handle_t *handle, hal_i2c_t *i2c, uint16_t address,
                float shunt_ohm, float max_current_a);
int ina219_deinit(ina219_handle_t *handle);

int ina219_read_bus_voltage(ina219_handle_t *handle, float *voltage);
int ina219_read_current(ina219_handle_t *handle, float *current);
int ina219_read_power(ina219_handle_t *handle, float *power);
int ina219_read(ina219_handle_t *handle, ina219_data_t *data);

#ifdef __cplusplus
}
#endif

#endif /* INA219_H */
