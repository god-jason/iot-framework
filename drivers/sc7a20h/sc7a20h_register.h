#ifndef SC7A20H_REGISTER_H
#define SC7A20H_REGISTER_H

/*
 * SC7A20H 三轴加速度计寄存器定义（I2C，16 位小端）。
 *
 * 寄存器布局兼容 LIS2DH 系列：WHO_AM_I(0x0F) 读回 0x11，
 * CTRL_REG1(0x20) 配置输出率与轴使能，CTRL_REG4(0x23) 配置量程与 BDU，
 * OUT_X_L~OUT_Z_H(0x28~0x2D) 为 6 字节小端数据。
 *
 * 器件工作于 12 位模式，数据在 16 位中左对齐：1 个有效 digit = 16 个 LSB，
 * 换算系数已按此折算。
 */

#define SC7A20H_DEFAULT_ADDRESS     0x18U   /* SA0 接 GND */
#define SC7A20H_ADDRESS_ALT         0x19U   /* SA0 接 VDD */

/* ---------------- 寄存器 ---------------- */
#define SC7A20H_REG_WHO_AM_I        0x0FU
#define SC7A20H_REG_CTRL_REG1       0x20U
#define SC7A20H_REG_CTRL_REG2       0x21U
#define SC7A20H_REG_CTRL_REG3       0x22U
#define SC7A20H_REG_CTRL_REG4       0x23U
#define SC7A20H_REG_CTRL_REG5       0x24U

#define SC7A20H_REG_OUT_X_L         0x28U
#define SC7A20H_REG_OUT_X_H         0x29U
#define SC7A20H_REG_OUT_Y_L         0x2AU
#define SC7A20H_REG_OUT_Y_H         0x2BU
#define SC7A20H_REG_OUT_Z_L         0x2CU
#define SC7A20H_REG_OUT_Z_H         0x2DU

/* ---------------- 器件标识 ---------------- */
#define SC7A20H_WHO_AM_I_VALUE      0x11U

/* ---------------- CTRL_REG1 ---------------- */
#define SC7A20H_CTRL1_AXIS_ENABLE   0x07U   /* X / Y / Z 使能 */
#define SC7A20H_CTRL1_LPEN_NORMAL   0x00U   /* 普通模式 */

/* 输出率编码（CTRL_REG1[7:4]） */
#define SC7A20H_ODR_CODE_1HZ        0x01U
#define SC7A20H_ODR_CODE_10HZ       0x02U
#define SC7A20H_ODR_CODE_25HZ       0x03U
#define SC7A20H_ODR_CODE_50HZ       0x04U
#define SC7A20H_ODR_CODE_100HZ      0x05U
#define SC7A20H_ODR_CODE_200HZ      0x06U
#define SC7A20H_ODR_CODE_400HZ      0x07U

/* ---------------- CTRL_REG4 ---------------- */
#define SC7A20H_CTRL4_BDU           0x80U   /* 读取期间锁定高低字节 */

/* 量程编码（CTRL_REG4[5:4]） */
#define SC7A20H_FS_CODE_2G          0x00U
#define SC7A20H_FS_CODE_4G          0x10U
#define SC7A20H_FS_CODE_8G          0x20U
#define SC7A20H_FS_CODE_16G         0x30U

/* ---------------- 灵敏度（g 每 LSB，16 位左对齐数据） ---------------- */
#define SC7A20H_SENS_G_PER_LSB_2G   0.0000625f   /* 1 mg/digit  */
#define SC7A20H_SENS_G_PER_LSB_4G   0.000125f    /* 2 mg/digit  */
#define SC7A20H_SENS_G_PER_LSB_8G   0.00025f     /* 4 mg/digit  */
#define SC7A20H_SENS_G_PER_LSB_16G  0.00075f     /* 12 mg/digit */

#endif /* SC7A20H_REGISTER_H */
