#ifndef LM75B_REGISTER_H
#define LM75B_REGISTER_H

/* LM75B 数字温度传感器寄存器定义（I2C，0x48 - 0x4F） */

#define LM75B_DEFAULT_ADDRESS       0x48U
#define LM75B_ADDRESS_MASK          0x7FU

/* 寄存器指针 */
#define LM75B_REG_TEMPERATURE       0x00U   /* 11 位，MSB 对齐，1 LSB = 0.125 ℃ */
#define LM75B_REG_CONFIG            0x01U
#define LM75B_REG_THYST             0x02U
#define LM75B_REG_TOS               0x03U

/* 配置寄存器位 */
#define LM75B_CONFIG_SHUTDOWN       0x01U
#define LM75B_CONFIG_COMP_INT       0x02U   /* 1: 中断模式 */
#define LM75B_CONFIG_OS_POL         0x04U   /* OS 输出极性 */
#define LM75B_CONFIG_FAULT_MASK     0x18U   /* 故障队列 bit4:3 */
#define LM75B_CONFIG_FAULT_1        0x00U
#define LM75B_CONFIG_FAULT_2        0x08U
#define LM75B_CONFIG_FAULT_4        0x10U
#define LM75B_CONFIG_FAULT_6        0x18U

/* 温度换算：0.125 ℃ / LSB，数据左对齐到高 11 位 */
#define LM75B_LSB_CELSIUS           0.125f
#define LM75B_DATA_SHIFT            5U

/* 上电转换时间（典型 100ms，保守 200ms） */
#define LM75B_CONV_DELAY_MS         200U

#endif /* LM75B_REGISTER_H */
