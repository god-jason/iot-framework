#ifndef HMC5883L_REGISTER_H
#define HMC5883L_REGISTER_H

/* HMC5883L 三轴磁力计寄存器与常量（Honeywell，I2C 0x1E） */

#define HMC5883L_DEFAULT_ADDRESS    0x1EU

#define HMC5883L_REG_CFG_A          0x00U
#define HMC5883L_REG_CFG_B          0x01U
#define HMC5883L_REG_MODE           0x02U
#define HMC5883L_REG_DATA_X_MSB     0x03U
#define HMC5883L_REG_DATA_X_LSB     0x04U
#define HMC5883L_REG_DATA_Z_MSB     0x05U
#define HMC5883L_REG_DATA_Z_LSB     0x06U
#define HMC5883L_REG_DATA_Y_MSB     0x07U
#define HMC5883L_REG_DATA_Y_LSB     0x08U
#define HMC5883L_REG_STATUS         0x09U
#define HMC5883L_REG_ID_A           0x0AU
#define HMC5883L_REG_ID_B           0x0BU
#define HMC5883L_REG_ID_C           0x0CU

/* 身份寄存器：'H' '4' '3'（HMC5883L 无 WHO_AM_I，用这三个字节代替） */
#define HMC5883L_ID_A_VALUE         0x48U   /* 'H' */
#define HMC5883L_ID_B_VALUE         0x34U   /* '4' */
#define HMC5883L_ID_C_VALUE         0x33U   /* '3' */
#define HMC5883L_ID_LEN             3U

/* CFG_A：MA[6:5] 平均次数，DO[4:2] 输出率，MS[1:0] 测量模式 */
#define HMC5883L_CFGA_MA_SHIFT      5U
#define HMC5883L_CFGA_MA_MASK       0x60U
#define HMC5883L_CFGA_DO_SHIFT      2U
#define HMC5883L_CFGA_DO_MASK       0x1CU
#define HMC5883L_CFGA_MS_MASK       0x03U

/* CFG_B：GN[7:5] 增益 */
#define HMC5883L_CFGB_GN_SHIFT      5U
#define HMC5883L_CFGB_GN_MASK       0xE0U

/* MODE：HS[7] 高速 I2C，MD[1:0] 模式 */
#define HMC5883L_MODE_HS            0x80U
#define HMC5883L_MODE_CONTINUOUS    0x00U
#define HMC5883L_MODE_SINGLE        0x01U
#define HMC5883L_MODE_IDLE          0x02U
#define HMC5883L_MODE_MD_MASK       0x03U

/* STATUS */
#define HMC5883L_STATUS_RDY         0x01U
#define HMC5883L_STATUS_LOCK        0x02U

/* 增益档位对应的灵敏度（LSB/Gauss），顺序与 CFG_B 的 GN 编码一致 */
#define HMC5883L_GAIN_1_3GA_LSB     1090.0f
#define HMC5883L_GAIN_1_9GA_LSB     820.0f
#define HMC5883L_GAIN_2_5GA_LSB     660.0f
#define HMC5883L_GAIN_4_0GA_LSB     440.0f
#define HMC5883L_GAIN_4_7GA_LSB     390.0f
#define HMC5883L_GAIN_5_6GA_LSB     330.0f
#define HMC5883L_GAIN_8_1GA_LSB     230.0f

/* 1 Gauss = 100 µT */
#define HMC5883L_UT_PER_GAUSS       100.0f

/* DEG_PER_LSB 在运行时由增益换算：ut_per_lsb = 100 / LSB_per_Gauss */

#define HMC5883L_DATA_BURST_LEN     6U
#define HMC5883L_START_DELAY_MS     2U
#define HMC5883L_DATA_TIMEOUT_MS    30U

#endif /* HMC5883L_REGISTER_H */
