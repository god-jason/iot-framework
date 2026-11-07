#ifndef INA226_REGISTER_H
#define INA226_REGISTER_H

/*
 * INA226 电流 / 电压 / 功率监测器寄存器定义（I2C，16 位大端）。
 *
 * 关键量纲（与 INA219 不同，务必注意）：
 *   - 分流电压寄存器 1 LSB = 2.5 uV（有符号）；
 *   - 总线电压寄存器 1 LSB = 1.25 mV，且数据不做移位（bit15:0 全部有效），
 *     INA219 的 bit15:3 + 4mV/LSB 布局不适用于本器件；
 *   - 功率寄存器 1 LSB = 25 * current_lsb；
 *   - 电流寄存器 1 LSB = current_lsb（由校准寄存器 CAL 决定）。
 */

#define INA226_DEFAULT_ADDRESS      0x40U
#define INA226_ADDRESS_MAX          0x4FU

/* ---------------- 寄存器 ---------------- */
#define INA226_REG_CONFIG           0x00U
#define INA226_REG_SHUNT_VOLTAGE    0x01U
#define INA226_REG_BUS_VOLTAGE      0x02U
#define INA226_REG_POWER            0x03U
#define INA226_REG_CURRENT          0x04U
#define INA226_REG_CALIBRATION      0x05U
#define INA226_REG_MASK_ENABLE      0x06U
#define INA226_REG_ALERT_LIMIT      0x07U
#define INA226_REG_MANUFACTURER_ID  0xFEU
#define INA226_REG_DIE_ID           0xFFU

/* ---------------- 器件标识 ---------------- */
#define INA226_MANUFACTURER_ID_VALUE  0x5449U   /* "TI" */
#define INA226_DIE_ID_VALUE           0x2260U

/* ---------------- CONFIG 字段 ---------------- */
#define INA226_CONFIG_RST           0x8000U
/* AVG[11:9]：0=1 次 ... 0x7=1024 次 */
#define INA226_CONFIG_AVG(n)        ((uint16_t)(((uint16_t)(n) & 0x07U) << 9))
#define INA226_CONFIG_AVG_16        INA226_CONFIG_AVG(2U)
/* VBUSCT[8:6] / VSHCT[5:3]：0x4 = 1.1ms */
#define INA226_CONFIG_VBUSCT(n)     ((uint16_t)(((uint16_t)(n) & 0x07U) << 6))
#define INA226_CONFIG_VSHCT(n)      ((uint16_t)(((uint16_t)(n) & 0x07U) << 3))
#define INA226_CONFIG_CT_1P1MS      0x04U
/* MODE[2:0]：0x7 = 分流 + 总线连续测量 */
#define INA226_CONFIG_MODE_SHUNT_BUS_CONT  0x07U

/* 默认配置：16 次平均、1.1ms 转换、分流 + 总线连续测量 -> 0x4527 */
#define INA226_CONFIG_DEFAULT \
    (uint16_t)(INA226_CONFIG_AVG_16 | \
               INA226_CONFIG_VBUSCT(INA226_CONFIG_CT_1P1MS) | \
               INA226_CONFIG_VSHCT(INA226_CONFIG_CT_1P1MS) | \
               INA226_CONFIG_MODE_SHUNT_BUS_CONT)

/* ---------------- 量纲 ---------------- */
#define INA226_SHUNT_LSB_V          0.0000025f   /* 2.5 uV */
#define INA226_BUS_LSB_V            0.00125f     /* 1.25 mV，不移位 */
#define INA226_BUS_VOLTAGE_MAX_V    40.96f       /* 0x7FFF * 1.25mV */
#define INA226_CAL_FACTOR           0.00512f
#define INA226_POWER_LSB_FACTOR     25.0f
#define INA226_CURRENT_LSB_DIVISOR  32768.0f
#define INA226_CAL_MAX              0xFFFFU

/* ---------------- 默认硬件参数 ---------------- */
#define INA226_DEFAULT_SHUNT_OHM      0.1f
#define INA226_DEFAULT_MAX_CURRENT_A  3.2f

#endif /* INA226_REGISTER_H */
