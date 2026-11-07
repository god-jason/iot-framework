#ifndef DA267_REGISTER_H
#define DA267_REGISTER_H

/*
 * DA267 三轴加速度计寄存器定义（I2C，16 位小端）。
 *
 * 寄存器布局与 LIS2DH 系列兼容：
 *   CTRL_REG1(0x20) 配置输出率与轴使能，CTRL_REG4(0x23) 配置量程与 BDU，
 *   OUT_X_L ~ OUT_Z_H(0x28~0x2D) 为 6 字节小端数据。
 *
 * 数据为 12 位有效、在 16 位中左对齐，因此 1 个有效 digit 对应 16 个 LSB，
 * 换算系数已按此折算（见 DA267_SENS_G_PER_LSB_*）。
 *
 * 该器件没有公开固定的 WHO_AM_I 期望值，驱动改用 probe + 配置寄存器回读自检。
 */

#define DA267_DEFAULT_ADDRESS       0x18U   /* SA0 接 GND */
#define DA267_ADDRESS_ALT           0x19U   /* SA0 接 VDD */

/* ---------------- 寄存器 ---------------- */
#define DA267_REG_WHO_AM_I          0x0FU
#define DA267_REG_CTRL_REG1         0x20U
#define DA267_REG_CTRL_REG2         0x21U
#define DA267_REG_CTRL_REG3         0x22U
#define DA267_REG_CTRL_REG4         0x23U
#define DA267_REG_CTRL_REG5         0x24U

#define DA267_REG_OUT_X_L           0x28U
#define DA267_REG_OUT_X_H           0x29U
#define DA267_REG_OUT_Y_L           0x2AU
#define DA267_REG_OUT_Y_H           0x2BU
#define DA267_REG_OUT_Z_L           0x2CU
#define DA267_REG_OUT_Z_H           0x2DU

/* ---------------- CTRL_REG1 ---------------- */
#define DA267_CTRL1_AXIS_ENABLE     0x07U   /* X / Y / Z 使能 */
#define DA267_CTRL1_LPEN_NORMAL     0x00U   /* 普通模式（非低功耗） */

/* 输出率编码（CTRL_REG1[7:4]） */
#define DA267_ODR_CODE_1HZ          0x01U
#define DA267_ODR_CODE_10HZ         0x02U
#define DA267_ODR_CODE_25HZ         0x03U
#define DA267_ODR_CODE_50HZ         0x04U
#define DA267_ODR_CODE_100HZ        0x05U
#define DA267_ODR_CODE_200HZ        0x06U
#define DA267_ODR_CODE_400HZ        0x07U

/* ---------------- CTRL_REG4 ---------------- */
#define DA267_CTRL4_BDU             0x80U   /* 读取期间锁定高低字节 */

/* 量程编码（CTRL_REG4[5:4]） */
#define DA267_FS_CODE_2G            0x00U
#define DA267_FS_CODE_4G            0x10U
#define DA267_FS_CODE_8G            0x20U
#define DA267_FS_CODE_16G           0x30U

/* ---------------- 灵敏度（g 每 LSB，16 位左对齐数据） ---------------- */
#define DA267_SENS_G_PER_LSB_2G     0.0000625f   /* 1 mg/digit  */
#define DA267_SENS_G_PER_LSB_4G     0.000125f    /* 2 mg/digit  */
#define DA267_SENS_G_PER_LSB_8G     0.00025f     /* 4 mg/digit  */
#define DA267_SENS_G_PER_LSB_16G    0.00075f     /* 12 mg/digit */

#endif /* DA267_REGISTER_H */
