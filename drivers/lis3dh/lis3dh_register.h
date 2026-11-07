#ifndef LIS3DH_REGISTER_H
#define LIS3DH_REGISTER_H

/*
 * LIS3DH 三轴加速度计寄存器定义（I2C，16 位小端）。
 *
 * WHO_AM_I(0x0F) 固定读回 0x33；CTRL_REG1(0x20) 配置输出率与轴使能，
 * CTRL_REG4(0x23) 配置量程、BDU 与高分辨率位，OUT_X_L~OUT_Z_H(0x28~0x2D) 为数据。
 *
 * 必须置 CTRL_REG4.HR=1 才能获得 12 位高分辨率输出（normal 模式为 10 位、
 * 灵敏度是 HR 模式的 4 倍），驱动固定按 HR 模式换算：
 * 1 个有效 digit = 16 个 LSB（16 位左对齐），见表中系数。
 */

#define LIS3DH_DEFAULT_ADDRESS      0x18U   /* SA0 接 GND */
#define LIS3DH_ADDRESS_ALT          0x19U   /* SA0 接 VDD */

/* ---------------- 寄存器 ---------------- */
#define LIS3DH_REG_WHO_AM_I         0x0FU
#define LIS3DH_REG_CTRL_REG1        0x20U
#define LIS3DH_REG_CTRL_REG2        0x21U
#define LIS3DH_REG_CTRL_REG3        0x22U
#define LIS3DH_REG_CTRL_REG4        0x23U
#define LIS3DH_REG_CTRL_REG5        0x24U

#define LIS3DH_REG_OUT_X_L          0x28U
#define LIS3DH_REG_OUT_X_H          0x29U
#define LIS3DH_REG_OUT_Y_L          0x2AU
#define LIS3DH_REG_OUT_Y_H          0x2BU
#define LIS3DH_REG_OUT_Z_L          0x2CU
#define LIS3DH_REG_OUT_Z_H          0x2DU

/* ---------------- 器件标识 ---------------- */
#define LIS3DH_WHO_AM_I_VALUE       0x33U

/* ---------------- CTRL_REG1 ---------------- */
#define LIS3DH_CTRL1_AXIS_ENABLE    0x07U   /* X / Y / Z 使能 */
#define LIS3DH_CTRL1_LPEN_NORMAL    0x00U   /* LPen = 0：普通 / 高分辨率模式 */

/* 输出率编码（CTRL_REG1[7:4]） */
#define LIS3DH_ODR_CODE_1HZ         0x01U
#define LIS3DH_ODR_CODE_10HZ        0x02U
#define LIS3DH_ODR_CODE_25HZ        0x03U
#define LIS3DH_ODR_CODE_50HZ        0x04U
#define LIS3DH_ODR_CODE_100HZ       0x05U
#define LIS3DH_ODR_CODE_200HZ       0x06U
#define LIS3DH_ODR_CODE_400HZ       0x07U

/* ---------------- CTRL_REG4 ---------------- */
#define LIS3DH_CTRL4_BDU            0x80U   /* 读取期间锁定高低字节 */
#define LIS3DH_CTRL4_HR            0x08U   /* 高分辨率模式（12 位） */

/* 量程编码（CTRL_REG4[5:4]） */
#define LIS3DH_FS_CODE_2G           0x00U
#define LIS3DH_FS_CODE_4G           0x10U
#define LIS3DH_FS_CODE_8G           0x20U
#define LIS3DH_FS_CODE_16G          0x30U

/* ---------------- 灵敏度（g 每 LSB，HR 模式 16 位左对齐数据） ---------------- */
#define LIS3DH_SENS_G_PER_LSB_2G    0.0000625f   /* 1 mg/digit  */
#define LIS3DH_SENS_G_PER_LSB_4G    0.000125f    /* 2 mg/digit  */
#define LIS3DH_SENS_G_PER_LSB_8G    0.00025f     /* 4 mg/digit  */
#define LIS3DH_SENS_G_PER_LSB_16G   0.00075f     /* 12 mg/digit */

#endif /* LIS3DH_REGISTER_H */
