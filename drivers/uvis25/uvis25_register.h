#ifndef UVIS25_REGISTER_H
#define UVIS25_REGISTER_H

/*
 * UVIS25 紫外线指数传感器寄存器定义（I2C，8 位寄存器地址 + 16 位数据）。
 *
 * 注意：以下寄存器地址与位域按 ST UVIS25 datasheet 整理，
 * 不同批次 / 封装在投产前必须与所用 datasheet 逐项核对，
 * 尤其是 CTRL_REG1 的 ODR 位域与 UV 指数输出寄存器地址。
 */

#define UVIS25_DEFAULT_ADDRESS      0x47U  /* SA0 接 VDD；SA0 接 GND 时为 0x46 */

#define UVIS25_REG_WHO_AM_I         0x0FU
#define UVIS25_REG_CTRL1            0x20U
#define UVIS25_REG_CTRL2            0x21U
#define UVIS25_REG_CTRL3            0x22U
#define UVIS25_REG_STATUS           0x23U
#define UVIS25_REG_UV_INDEX_L       0x26U
#define UVIS25_REG_UV_INDEX_H       0x27U

/* WHO_AM_I 期望值 */
#define UVIS25_WHO_AM_I_VALUE       0xCAU

/* CTRL_REG1 位域 */
#define UVIS25_CTRL1_PD             0x80U  /* 1 = power-down */
#define UVIS25_CTRL1_BDU            0x04U  /* 块数据更新：读期间不刷新输出 */
#define UVIS25_CTRL1_ODR_SHIFT      4U
#define UVIS25_CTRL1_ODR_MASK       (0x07U << UVIS25_CTRL1_ODR_SHIFT)

/* ODR 编码（写入 CTRL_REG1 的 ODR 位域） */
#define UVIS25_ODR_CODE_POWER_DOWN  0x00U
#define UVIS25_ODR_CODE_1HZ         0x01U
#define UVIS25_ODR_CODE_7HZ         0x02U
#define UVIS25_ODR_CODE_12HZ        0x03U

/* STATUS 位域 */
#define UVIS25_STATUS_UV_DA         0x01U  /* 新的 UV 数据就绪 */

/* 输出数据寄存器为小端：先低字节后高字节 */
#define UVIS25_DATA_LITTLE_ENDIAN   1

/*
 * UV 指数换算：器件输出 16 位无符号原始值，默认按 100 LSB = 1 UVI 估算。
 * 该比例必须按 datasheet 给出的灵敏度 / 标定结果确认后再使用。
 */
#define UVIS25_RAW_PER_UVI          100.0f

/* 轮询数据就绪的超时（ms），按最低 ODR(1Hz) 留余量 */
#define UVIS25_POLL_TIMEOUT_MS      1500U
#define UVIS25_POWER_ON_DELAY_MS    10U

#endif /* UVIS25_REGISTER_H */
