#ifndef MAX30102_REGISTER_H
#define MAX30102_REGISTER_H

/* MAX30102 心率 / 血氧传感器寄存器定义（I2C 0x57） */

#define MAX30102_ADDRESS_DEFAULT        0x57U
#define MAX30102_PART_ID_VALUE          0x15U

/* 寄存器 */
#define MAX30102_REG_INTR_STATUS_1      0x00U
#define MAX30102_REG_INTR_STATUS_2      0x01U
#define MAX30102_REG_INTR_ENABLE_1      0x02U
#define MAX30102_REG_INTR_ENABLE_2      0x03U
#define MAX30102_REG_FIFO_WR_PTR        0x04U
#define MAX30102_REG_OVF_COUNTER        0x05U
#define MAX30102_REG_FIFO_RD_PTR        0x06U
#define MAX30102_REG_FIFO_DATA          0x07U
#define MAX30102_REG_FIFO_CONFIG        0x08U
#define MAX30102_REG_MODE_CONFIG        0x09U
#define MAX30102_REG_SPO2_CONFIG        0x0AU
#define MAX30102_REG_LED1_PA            0x0CU  /* RED */
#define MAX30102_REG_LED2_PA            0x0DU  /* IR */
#define MAX30102_REG_PILOT_PA           0x10U
#define MAX30102_REG_MULTI_LED_CTRL1    0x11U
#define MAX30102_REG_MULTI_LED_CTRL2    0x12U
#define MAX30102_REG_TEMP_INTR          0x1FU  /* 整数部分（0.0625℃/LSB，有符号） */
#define MAX30102_REG_TEMP_FRAC          0x20U  /* 小数部分 */
#define MAX30102_REG_TEMP_CONFIG        0x21U  /* 写 1 触发一次温度采样 */
#define MAX30102_REG_REV_ID             0xFEU
#define MAX30102_REG_PART_ID            0xFFU

/* MODE_CONFIG 位 */
#define MAX30102_MODE_SHUTDOWN          0x80U
#define MAX30102_MODE_RESET             0x40U
#define MAX30102_MODE_MASK              0x07U

/* FIFO_CONFIG 位 */
#define MAX30102_FIFO_ROLLOVER_EN       0x10U
#define MAX30102_FIFO_A_FULL_MASK       0x0FU
#define MAX30102_FIFO_A_FULL_15         0x0FU /* 15 个空余位置时触发 almost full 中断 */

/* SPO2_CONFIG 位 */
#define MAX30102_SPO2_ADC_RANGE_4096    0x20U  /* 全量程 4096nA */
#define MAX30102_SPO2_SR_100HZ          0x04U  /* 采样率 100Hz << 2 */
#define MAX30102_SPO2_PW_411US_18BIT    0x03U  /* 脉冲宽度 411us / 18 位 */

/* 温度 */
#define MAX30102_TEMP_FRAC_LSB_C        0.0625f
#define MAX30102_TEMP_MEASURE_MS        30U

/* FIFO */
#define MAX30102_FIFO_DEPTH             32U
#define MAX30102_FIFO_MAX_BYTES         (MAX30102_FIFO_DEPTH * 6U)
#define MAX30102_FIFO_PTR_MASK          0x1FU
#define MAX30102_SAMPLE_BYTES_PER_CH    3U     /* 18 位左对齐存于 3 字节 */
#define MAX30102_ADC_MASK               0x03FFFFUL

#endif /* MAX30102_REGISTER_H */
