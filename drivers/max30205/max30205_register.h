#ifndef MAX30205_REGISTER_H
#define MAX30205_REGISTER_H

/* MAX30205 人体体温传感器寄存器定义（I2C，0x48 - 0x4B） */

#define MAX30205_DEFAULT_ADDRESS    0x48U
#define MAX30205_ADDRESS_MASK       0x7FU
#define MAX30205_ADDRESS_MAX        0x4BU

/* 寄存器 */
#define MAX30205_REG_TEMPERATURE    0x00U   /* 16 位有符号，1 LSB = 1/256 ℃ */
#define MAX30205_REG_CONFIG         0x01U
#define MAX30205_REG_THYST          0x02U   /* 迟滞阈值 */
#define MAX30205_REG_TOS            0x03U   /* 过温阈值 */

/* 配置寄存器位 */
#define MAX30205_CONFIG_SHDN        0x0001U /* 1: 关断 */
#define MAX30205_CONFIG_COMP_INT    0x0002U /* 1: 中断模式，0: 比较器模式 */
#define MAX30205_CONFIG_OS_POL      0x0004U /* OS 输出极性 */
#define MAX30205_CONFIG_FAULT_MASK  0x0018U /* 故障队列（bit4:3） */
#define MAX30205_CONFIG_FAULT_1     0x0000U
#define MAX30205_CONFIG_FAULT_2     0x0008U
#define MAX30205_CONFIG_FAULT_4     0x0010U
#define MAX30205_CONFIG_FAULT_6     0x0018U
#define MAX30205_CONFIG_TIMEOUT     0x0020U /* 1: 使能 SMBus 超时 */

/* 温度换算：1 LSB = 1/256 ℃ */
#define MAX30205_LSB_CELSIUS        0.00390625f
/* 上电后首次转换时间（典型 50ms，保守 100ms） */
#define MAX30205_CONV_DELAY_MS      100U

#endif /* MAX30205_REGISTER_H */
