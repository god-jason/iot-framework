#ifndef LIS2DH12_REGISTER_H
#define LIS2DH12_REGISTER_H

/*
 * LIS2DH12 三轴加速度计寄存器定义（I2C，16 位小端）。
 *
 * WHO_AM_I(0x0F) 固定读回 0x33；CTRL_REG1(0x20) 配置输出率与轴使能，
 * CTRL_REG4(0x23) 配置量程与 BDU，OUT_X_L~OUT_Z_H(0x28~0x2D) 为数据。
 *
 * 复位后 CTRL_REG1 的 LPen=0，处于 12 位高分辨率模式，数据在 16 位中左对齐：
 * 1 个有效 digit = 16 个 LSB，换算系数已按此折算。
 */

#define LIS2DH12_DEFAULT_ADDRESS    0x18U   /* SA0 接 GND */
#define LIS2DH12_ADDRESS_ALT        0x19U   /* SA0 接 VDD */

/* ---------------- 寄存器 ---------------- */
#define LIS2DH12_REG_WHO_AM_I       0x0FU
#define LIS2DH12_REG_CTRL_REG1      0x20U
#define LIS2DH12_REG_CTRL_REG2      0x21U
#define LIS2DH12_REG_CTRL_REG3      0x22U
#define LIS2DH12_REG_CTRL_REG4      0x23U
#define LIS2DH12_REG_CTRL_REG5      0x24U

#define LIS2DH12_REG_OUT_X_L        0x28U
#define LIS2DH12_REG_OUT_X_H        0x29U
#define LIS2DH12_REG_OUT_Y_L        0x2AU
#define LIS2DH12_REG_OUT_Y_H        0x2BU
#define LIS2DH12_REG_OUT_Z_L        0x2CU
#define LIS2DH12_REG_OUT_Z_H        0x2DU

/* ---------------- 器件标识 ---------------- */
#define LIS2DH12_WHO_AM_I_VALUE     0x33U

/* ---------------- CTRL_REG1 ---------------- */
#define LIS2DH12_CTRL1_AXIS_ENABLE  0x07U   /* X / Y / Z 使能 */
#define LIS2DH12_CTRL1_LPEN_HR      0x00U   /* LPen = 0：高分辨率（12 位） */

/* 输出率编码（CTRL_REG1[7:4]） */
#define LIS2DH12_ODR_CODE_1HZ       0x01U
#define LIS2DH12_ODR_CODE_10HZ      0x02U
#define LIS2DH12_ODR_CODE_25HZ      0x03U
#define LIS2DH12_ODR_CODE_50HZ      0x04U
#define LIS2DH12_ODR_CODE_100HZ     0x05U
#define LIS2DH12_ODR_CODE_200HZ     0x06U
#define LIS2DH12_ODR_CODE_400HZ     0x07U

/* ---------------- CTRL_REG4 ---------------- */
#define LIS2DH12_CTRL4_BDU          0x80U   /* 读取期间锁定高低字节 */

/* 量程编码（CTRL_REG4[5:4]） */
#define LIS2DH12_FS_CODE_2G         0x00U
#define LIS2DH12_FS_CODE_4G         0x10U
#define LIS2DH12_FS_CODE_8G         0x20U
#define LIS2DH12_FS_CODE_16G        0x30U

/* ---------------- 灵敏度（g 每 LSB，16 位左对齐数据） ---------------- */
#define LIS2DH12_SENS_G_PER_LSB_2G   0.0000625f   /* 1 mg/digit  */
#define LIS2DH12_SENS_G_PER_LSB_4G   0.000125f    /* 2 mg/digit  */
#define LIS2DH12_SENS_G_PER_LSB_8G   0.00025f     /* 4 mg/digit  */
#define LIS2DH12_SENS_G_PER_LSB_16G  0.00075f     /* 12 mg/digit */

#endif /* LIS2DH12_REGISTER_H */
