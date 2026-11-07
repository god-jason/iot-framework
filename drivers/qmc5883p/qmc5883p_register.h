#ifndef QMC5883P_REGISTER_H
#define QMC5883P_REGISTER_H

/*
 * QMC5883P 三轴磁力计寄存器与常量（QST，I2C 7 位地址 0x2C）。
 *
 * 注意：QMC5883P 与 QMC5883L 寄存器映射不同——数据从 0x01 开始（0x00 是
 * Chip ID），控制寄存器在 0x0A/0x0B，不能照搬 QMC5883L 的 0x00/0x09。
 */

#define QMC5883P_DEFAULT_ADDRESS    0x2CU

#define QMC5883P_REG_CHIP_ID        0x00U
#define QMC5883P_CHIP_ID_VALUE      0x80U

/* 数据寄存器：三轴各 16 位，小端 */
#define QMC5883P_REG_DATA_X_LSB     0x01U
#define QMC5883P_REG_DATA_X_MSB     0x02U
#define QMC5883P_REG_DATA_Y_LSB     0x03U
#define QMC5883P_REG_DATA_Y_MSB     0x04U
#define QMC5883P_REG_DATA_Z_LSB     0x05U
#define QMC5883P_REG_DATA_Z_MSB     0x06U

#define QMC5883P_REG_STATUS         0x09U
#define QMC5883P_REG_CTRL1          0x0AU
#define QMC5883P_REG_CTRL2          0x0BU

/* STATUS */
#define QMC5883P_STATUS_DRDY        0x01U
#define QMC5883P_STATUS_OVL         0x02U   /* 量程溢出，本次数据不可信 */
#define QMC5883P_STATUS_DOR         0x04U   /* 数据跳过 */

/* CTRL1：MODE[1:0] 工作模式，ODR[3:2] 输出率，OSR[5:4] 过采样 */
#define QMC5883P_CTRL1_MODE_MASK    0x03U
#define QMC5883P_CTRL1_ODR_SHIFT    2U
#define QMC5883P_CTRL1_ODR_MASK     0x0CU
#define QMC5883P_CTRL1_OSR_SHIFT    4U
#define QMC5883P_CTRL1_OSR_MASK     0x30U

#define QMC5883P_MODE_SUSPEND       0x00U
#define QMC5883P_MODE_NORMAL        0x01U   /* 连续测量 */
#define QMC5883P_MODE_SINGLE        0x02U

#define QMC5883P_ODR_10HZ           0x00U
#define QMC5883P_ODR_50HZ           0x01U
#define QMC5883P_ODR_100HZ          0x02U
#define QMC5883P_ODR_200HZ          0x03U

#define QMC5883P_OSR_8              0x00U
#define QMC5883P_OSR_4              0x01U
#define QMC5883P_OSR_2              0x02U
#define QMC5883P_OSR_1              0x03U

/* CTRL2：SET/RESET[1:0] 置位/复位方式，RNG[3:2] 量程 */
#define QMC5883P_CTRL2_RNG_SHIFT    2U
#define QMC5883P_CTRL2_RNG_MASK     0x0CU

/* 量程编码（RNG[3:2]），同时作为驱动侧灵敏度索引。
 * 编码顺序按数据手册给出（2G→8G→12G→30G），兼容片可能不同，
 * 若实测出现比例偏差请以此表为准重新标定。 */
#define QMC5883P_RNG_2G             0x00U
#define QMC5883P_RNG_8G             0x01U
#define QMC5883P_RNG_12G            0x02U
#define QMC5883P_RNG_30G            0x03U

/* 灵敏度（LSB/Gauss），数据手册典型值 */
#define QMC5883P_LSB_PER_GAUSS_2G   15000.0f
#define QMC5883P_LSB_PER_GAUSS_8G   3750.0f
#define QMC5883P_LSB_PER_GAUSS_12G  2500.0f
#define QMC5883P_LSB_PER_GAUSS_30G  1000.0f

/* 1 Gauss = 100 µT */
#define QMC5883P_UT_PER_GAUSS       100.0f

/* 默认配置：MODE=normal，ODR=100Hz，OSR=8 => CTRL1 0x09；RNG=8G => CTRL2 0x04。
 * 选 8G 是为了靠近磁铁/电机时不溢出，地球磁场（25~65µT）仍有余量。 */
#define QMC5883P_CTRL1_DEFAULT      0x09U
#define QMC5883P_CTRL2_DEFAULT      0x04U

#define QMC5883P_DATA_LEN           6U

/* 时序（ms） */
#define QMC5883P_RESET_DELAY_MS     2U
#define QMC5883P_START_DELAY_MS     10U
#define QMC5883P_DATA_TIMEOUT_MS    50U

#define QMC5883P_RAD_TO_DEG         57.2957795f

#endif /* QMC5883P_REGISTER_H */
