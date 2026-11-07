#ifndef INA219_REGISTER_H
#define INA219_REGISTER_H

/* INA219 寄存器与量程定义 */

#define INA219_DEFAULT_ADDRESS      0x40U
#define INA219_ADDRESS_MAX          0x4FU

#define INA219_REG_CONFIG           0x00U
#define INA219_REG_SHUNT_VOLTAGE    0x01U
#define INA219_REG_BUS_VOLTAGE      0x02U
#define INA219_REG_POWER            0x03U
#define INA219_REG_CURRENT          0x04U
#define INA219_REG_CALIBRATION      0x05U

/* 配置：16V 总线，PGA /8（±320mV），12bit，连续测量分流+总线 */
#define INA219_CONFIG_16V_320MV_12BIT_CONT  0x399FU

/* LSB 定义 */
#define INA219_SHUNT_LSB_V          0.00001f    /* 10 uV */
#define INA219_BUS_LSB_V            0.004f      /* 4 mV */
#define INA219_BUS_VOLTAGE_MASK     0xFFF8U     /* bit15:3 */
#define INA219_CAL_FACTOR           0.04096f
#define INA219_POWER_LSB_FACTOR     20.0f

#define INA219_DEFAULT_SHUNT_OHM    0.1f
#define INA219_DEFAULT_MAX_CURRENT  3.2f

#endif /* INA219_REGISTER_H */
