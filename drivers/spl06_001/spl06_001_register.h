#ifndef SPL06_001_REGISTER_H
#define SPL06_001_REGISTER_H

/*
 * SPL06-001 寄存器 / 校准系数 / 缩放因子定义。
 *
 * 数据来源：Goertek SPL06-001 datasheet。
 *   - 产品 ID 0x10（寄存器 0x0D）
 *   - 数据：0x00..0x02 压力 24 位（补码，MSB 在前），0x03..0x05 温度 24 位
 *   - 校准系数 COEF：0x10..0x21，共 18 字节
 */

#define SPL06_001_DEFAULT_ADDRESS   0x76U
#define SPL06_001_ADDRESS_ALT       0x77U

#define SPL06_001_REG_PSR_B2        0x00U   /* 压力 24 位，MSB 在前 */
#define SPL06_001_REG_TMP_B2        0x03U   /* 温度 24 位，MSB 在前 */
#define SPL06_001_REG_PRS_CFG       0x06U
#define SPL06_001_REG_TMP_CFG       0x07U
#define SPL06_001_REG_MEAS_CFG      0x08U
#define SPL06_001_REG_CFG           0x09U
#define SPL06_001_REG_INT_STS       0x0AU
#define SPL06_001_REG_FIFO_STS      0x0BU
#define SPL06_001_REG_RESET         0x0CU
#define SPL06_001_REG_ID            0x0DU
#define SPL06_001_REG_COEF          0x10U   /* 18 字节 */

#define SPL06_001_CHIP_ID           0x10U
#define SPL06_001_RESET_VALUE       0x09U

/* MEAS_CFG：状态位与测量控制 */
#define SPL06_001_STATUS_COEF_RDY   0x80U
#define SPL06_001_STATUS_SENSOR_RDY 0x40U
#define SPL06_001_STATUS_TMP_RDY    0x20U
#define SPL06_001_STATUS_PRS_RDY    0x10U
#define SPL06_001_MEAS_MASK         0x07U
#define SPL06_001_MEAS_IDLE         0x00U
#define SPL06_001_MEAS_CONT_PT      0x05U   /* 连续压力 + 温度测量 */

/* PRS_CFG / TMP_CFG：bit6:4 速率，bit3:0 过采样（0 = x1 ... 7 = x128） */
#define SPL06_001_PRS_PRC_X8        0x03U
#define SPL06_001_TMP_PRC_X8        0x03U
#define SPL06_001_PRS_CFG_DEFAULT   ((uint8_t)(SPL06_001_PRS_PRC_X8))
#define SPL06_001_TMP_CFG_DEFAULT   ((uint8_t)(SPL06_001_TMP_PRC_X8))
#define SPL06_001_CFG_DEFAULT       0x00U   /* 不使用中断 */

#define SPL06_001_RESET_DELAY_MS    10U
#define SPL06_001_MEASURE_DELAY_MS  40U
#define SPL06_001_READY_TIMEOUT_MS  200U

#define SPL06_001_COEF_SIZE         18U
#define SPL06_001_DATA_SIZE         6U

/* 系数与缩放因子数量 */
#define SPL06_001_SCALE_COUNT       8U

/* 12 / 20 位补码符号扩展掩码 */
#define SPL06_001_SIGN12_MASK       0x0800U
#define SPL06_001_SIGN12_EXT        0xF000U
#define SPL06_001_SIGN20_MASK       0x080000U
#define SPL06_001_SIGN20_EXT        0xFFF00000U

/* 输出合理区间 */
#define SPL06_001_PRESSURE_MIN_PA   30000.0f
#define SPL06_001_PRESSURE_MAX_PA   110000.0f

#endif /* SPL06_001_REGISTER_H */
