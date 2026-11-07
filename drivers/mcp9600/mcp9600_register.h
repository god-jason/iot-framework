#ifndef MCP9600_REGISTER_H
#define MCP9600_REGISTER_H

/* MCP9600 热电偶 EMF 到温度转换器寄存器定义（I2C，0x60 - 0x67） */

#define MCP9600_DEFAULT_ADDRESS     0x60U
#define MCP9600_ADDRESS_MASK        0x7FU
#define MCP9600_ADDRESS_MIN         0x60U
#define MCP9600_ADDRESS_MAX         0x67U

/* 寄存器 */
#define MCP9600_REG_HOT_JUNCTION     0x00U   /* 热端温度，16 位有符号，0.0625 ℃/LSB */
#define MCP9600_REG_JUNCTION_DELTA   0x01U   /* 冷热端温差，同分辨率 */
#define MCP9600_REG_COLD_JUNCTION    0x02U   /* 冷端温度 */
#define MCP9600_REG_RAW_ADC          0x03U   /* 24 位有符号原始 ADC 码 */
#define MCP9600_REG_STATUS           0x04U
#define MCP9600_REG_SENSOR_CONFIG    0x05U   /* 热电偶类型 + 数字滤波 */
#define MCP9600_REG_DEVICE_CONFIG    0x06U
#define MCP9600_REG_ALERT1_CONFIG    0x07U
#define MCP9600_REG_ALERT2_CONFIG    0x08U
#define MCP9600_REG_ALERT3_CONFIG    0x09U
#define MCP9600_REG_ALERT4_CONFIG    0x0AU
#define MCP9600_REG_ALERT1_HYST      0x0BU
#define MCP9600_REG_ALERT2_HYST      0x0CU
#define MCP9600_REG_ALERT3_HYST      0x0DU
#define MCP9600_REG_ALERT4_HYST      0x0EU
#define MCP9600_REG_DEVICE_ID        0x20U   /* 固定为 0x40 */
#define MCP9600_REG_REVISION         0x21U

#define MCP9600_DEVICE_ID_VALUE      0x40U

/* 传感器配置寄存器（0x05）：bit6:4 热电偶类型，bit2:0 滤波系数 */
#define MCP9600_TC_SHIFT             4U
#define MCP9600_TC_MASK              0x70U
#define MCP9600_TC_TYPE_K            0x00U
#define MCP9600_TC_TYPE_J            0x01U
#define MCP9600_TC_TYPE_T            0x02U
#define MCP9600_TC_TYPE_N            0x03U
#define MCP9600_TC_TYPE_E            0x04U
#define MCP9600_TC_TYPE_B            0x05U
#define MCP9600_TC_TYPE_R            0x06U
#define MCP9600_TC_TYPE_S            0x07U
#define MCP9600_FILTER_MASK          0x07U

/* 器件配置寄存器（0x06） */
#define MCP9600_DEVCFG_SHUTDOWN_MSK  0x03U
#define MCP9600_DEVCFG_SHUTDOWN      0x01U   /* 01: 关断模式 */
#define MCP9600_DEVCFG_BURST_MSK     0x0CU
#define MCP9600_DEVCFG_ADC_RES_MSK   0x30U   /* 00: 18 位，01: 16 位，10: 14 位，11: 12 位 */
#define MCP9600_DEVCFG_CJ_RES        0x40U   /* 0: 0.0625 ℃，1: 0.03125 ℃ */

/* 温度换算 */
#define MCP9600_LSB_CELSIUS          0.0625f

/* 时序（ms）：热电偶类型 / 滤波系数变更后需重新建立转换 */
#define MCP9600_POWER_ON_DELAY_MS    100U
#define MCP9600_CONFIG_DELAY_MS      50U

#endif /* MCP9600_REGISTER_H */
