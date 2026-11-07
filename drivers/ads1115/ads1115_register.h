#ifndef ADS1115_REGISTER_H
#define ADS1115_REGISTER_H

/* ADS1115 16 位 4 通道 Δ-Σ ADC 寄存器与位定义（I2C，地址 0x48~0x4B） */

#define ADS1115_DEFAULT_ADDRESS     0x48U
#define ADS1115_ADDRESS_MAX         0x4BU

/* 寄存器指针 */
#define ADS1115_REG_CONVERSION      0x00U   /* 转换结果（只读，大端 16 位） */
#define ADS1115_REG_CONFIG          0x01U   /* 配置寄存器（16 位） */
#define ADS1115_REG_LO_THRESH       0x02U   /* 比较器低阈值 */
#define ADS1115_REG_HI_THRESH       0x03U   /* 比较器高阈值 */

/* 配置寄存器位（16 位大端拼装） */
#define ADS1115_CFG_OS              0x8000U /* 1: 启动单次转换；读回 1 表示空闲 */
#define ADS1115_CFG_MUX_SHIFT       12U
#define ADS1115_CFG_MUX_MASK        0x7000U
#define ADS1115_CFG_PGA_SHIFT       9U
#define ADS1115_CFG_PGA_MASK        0x0E00U
#define ADS1115_CFG_MODE_SINGLE     0x0100U /* 1: 单次转换模式 */
#define ADS1115_CFG_DR_SHIFT        5U
#define ADS1115_CFG_DR_MASK         0x00E0U
#define ADS1115_CFG_COMP_QUE_OFF    0x0003U /* 关闭比较器 */

/* 比较器阈值默认值：低位 0x8000、高位 0x7FFF 使比较器永不触发 */
#define ADS1115_LO_THRESH_DEFAULT   0x8000U
#define ADS1115_HI_THRESH_DEFAULT   0x7FFFU

/* 单次转换轮询额外余量（ms） */
#define ADS1115_CONV_EXTRA_MS       20U

/* PGA 对应的满量程电压（mV） */
#define ADS1115_FS_6V144_MV         6144U
#define ADS1115_FS_4V096_MV         4096U
#define ADS1115_FS_2V048_MV         2048U
#define ADS1115_FS_1V024_MV         1024U
#define ADS1115_FS_0V512_MV         512U
#define ADS1115_FS_0V256_MV         256U

#endif /* ADS1115_REGISTER_H */
