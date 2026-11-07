#ifndef QMC5883L_REGISTER_H
#define QMC5883L_REGISTER_H

/* QMC5883L 三轴磁力计寄存器与常量（QST，I2C 7 位地址 0x0D） */

#define QMC5883L_DEFAULT_ADDRESS    0x0DU

/* 数据寄存器：三轴各 16 位，小端（低字节在前） */
#define QMC5883L_REG_DATA_X_LSB     0x00U
#define QMC5883L_REG_DATA_X_MSB     0x01U
#define QMC5883L_REG_DATA_Y_LSB     0x02U
#define QMC5883L_REG_DATA_Y_MSB     0x03U
#define QMC5883L_REG_DATA_Z_LSB     0x04U
#define QMC5883L_REG_DATA_Z_MSB     0x05U
#define QMC5883L_REG_STATUS         0x06U
#define QMC5883L_REG_TEMP_LSB       0x07U
#define QMC5883L_REG_TEMP_MSB       0x08U
#define QMC5883L_REG_CTRL1          0x09U
#define QMC5883L_REG_CTRL2          0x0AU
#define QMC5883L_REG_SET_RESET      0x0BU
#define QMC5883L_REG_CHIP_ID        0x0DU

/* 识别寄存器：QST 原厂固件返回 0xFF；兼容片/国产替代可能返回其它值，
 * 因此只作告警，不作为初始化失败条件（存在性由 I2C probe 证明）。 */
#define QMC5883L_CHIP_ID_VALUE      0xFFU

/* CTRL1：OSR[7:6] 过采样，RNG[5:4] 量程，ODR[3:2] 输出率，MODE[1:0] 模式 */
#define QMC5883L_CTRL1_OSR_SHIFT    6U
#define QMC5883L_CTRL1_OSR_MASK     0xC0U
#define QMC5883L_CTRL1_RNG_SHIFT    4U
#define QMC5883L_CTRL1_RNG_MASK     0x30U
#define QMC5883L_CTRL1_ODR_SHIFT    2U
#define QMC5883L_CTRL1_ODR_MASK     0x0CU
#define QMC5883L_CTRL1_MODE_SHIFT   0U
#define QMC5883L_CTRL1_MODE_MASK    0x03U

#define QMC5883L_OSR_512            0x00U
#define QMC5883L_OSR_256            0x01U
#define QMC5883L_OSR_128            0x02U
#define QMC5883L_OSR_64             0x03U

#define QMC5883L_ODR_10HZ           0x00U
#define QMC5883L_ODR_50HZ           0x01U
#define QMC5883L_ODR_100HZ          0x02U
#define QMC5883L_ODR_200HZ          0x03U

#define QMC5883L_MODE_STANDBY       0x00U
#define QMC5883L_MODE_CONTINUOUS    0x01U

/* 量程编码（RNG[5:4]），同时作为驱动侧灵敏度索引 */
#define QMC5883L_RNG_2G             0x00U
#define QMC5883L_RNG_8G             0x01U

/* 默认配置：OSR=512，RNG=8G，ODR=100Hz，连续测量 => 0x19。
 * 选 8G 而非 2G 是为了在靠近磁铁/电机时不溢出；地球磁场约 25~65µT，
 * 两种量程都足以覆盖，需要更高分辨率时用 qmc5883l_set_range() 切到 2G。 */
#define QMC5883L_CTRL1_DEFAULT      0x19U

/* 灵敏度（LSB/Gauss），数据手册典型值 */
#define QMC5883L_LSB_PER_GAUSS_2G   12000.0f
#define QMC5883L_LSB_PER_GAUSS_8G   3000.0f

/* 1 Gauss = 100 µT */
#define QMC5883L_UT_PER_GAUSS       100.0f

/* CTRL2：SOFT_RST[7] 软复位，ROL_PNT[6] 指针翻转，INT_ENB[0] 中断使能 */
#define QMC5883L_CTRL2_SOFT_RST     0x80U
#define QMC5883L_CTRL2_ROL_PNT      0x40U
#define QMC5883L_CTRL2_INT_ENB      0x01U

/* STATUS */
#define QMC5883L_STATUS_DRDY        0x01U
#define QMC5883L_STATUS_OVL         0x02U   /* 量程溢出，本次数据不可信 */
#define QMC5883L_STATUS_DOR         0x04U   /* 数据跳过（读取速度跟不上输出率） */

/* SET/RESET 周期寄存器推荐值 */
#define QMC5883L_SET_RESET_PERIOD   0x01U

#define QMC5883L_DATA_LEN           6U

/* 时序（ms） */
#define QMC5883L_RESET_DELAY_MS     2U
#define QMC5883L_START_DELAY_MS     10U
#define QMC5883L_DATA_TIMEOUT_MS    50U

#define QMC5883L_RAD_TO_DEG         57.2957795f

#endif /* QMC5883L_REGISTER_H */
