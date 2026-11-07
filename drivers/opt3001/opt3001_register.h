#ifndef OPT3001_REGISTER_H
#define OPT3001_REGISTER_H

/* OPT3001 环境光传感器寄存器与位域定义（I2C，16 位大端寄存器） */

#define OPT3001_DEFAULT_ADDRESS     0x44U
#define OPT3001_ADDRESS_ALT         0x45U  /* ADDR 接 VDD；0x46 接 SDA，0x47 接 SCL */

#define OPT3001_REG_RESULT          0x00U
#define OPT3001_REG_CONFIG          0x01U
#define OPT3001_REG_MANUFACTURER_ID 0x7EU
#define OPT3001_REG_DEVICE_ID       0x7FU

#define OPT3001_MANUFACTURER_ID     0x5449U  /* 'TI' */
#define OPT3001_DEVICE_ID           0x3001U

/* CONFIG 位域 */
#define OPT3001_CONFIG_RN           (1U << 15)  /* 量程选择位 */
#define OPT3001_CONFIG_CT_SHIFT     12U         /* 转换时间 CT[2:0] */
#define OPT3001_CONFIG_CT_MASK      (0x07U << OPT3001_CONFIG_CT_SHIFT)
#define OPT3001_CONFIG_M_MASK       (0x03U << 10)  /* 工作模式 M[1:0] */
#define OPT3001_CONFIG_M_SHUTDOWN   (0x00U << 10)
#define OPT3001_CONFIG_M_SINGLE     (0x01U << 10)
#define OPT3001_CONFIG_M_CONTINUOUS (0x03U << 10)
#define OPT3001_CONFIG_OVF          (0x03U << 8)   /* 量程溢出标志（只读） */
#define OPT3001_CONFIG_CRF          (1U << 7)      /* 转换就绪标志（只读） */
#define OPT3001_CONFIG_L            (1U << 4)      /* 中断上报模式 */

/* CONFIG.CT 取值：datasheet 仅定义 100ms / 800ms 两档，其余编码保留 */
#define OPT3001_CT_100MS            0x00U
#define OPT3001_CT_800MS            0x01U

/* RESULT：高 12 位为尾数，低 4 位为指数，lux = 尾数 * 0.01 * 2^指数 */
#define OPT3001_RESULT_MANTISSA(r)  ((uint16_t)((r) >> 4))
#define OPT3001_RESULT_EXPONENT(r)  ((uint8_t)((r) & 0x0FU))
#define OPT3001_LUX_LSB             0.01f
#define OPT3001_RESULT_OVERFLOW     0xFFFFU

/* 转换等待时间（ms），比 datasheet 标称值留余量 */
#define OPT3001_DELAY_100MS         120U
#define OPT3001_DELAY_800MS         850U
#define OPT3001_POWER_ON_DELAY_MS   2U

#endif /* OPT3001_REGISTER_H */
