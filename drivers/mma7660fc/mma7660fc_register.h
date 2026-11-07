#ifndef MMA7660FC_REGISTER_H
#define MMA7660FC_REGISTER_H

/* MMA7660FC 三轴加速度计寄存器与常量（NXP / Freescale，I2C 0x4C） */

#define MMA7660FC_ADDRESS           0x4CU

#define MMA7660FC_REG_XOUT          0x00U
#define MMA7660FC_REG_YOUT          0x01U
#define MMA7660FC_REG_ZOUT          0x02U
#define MMA7660FC_REG_TILT          0x03U
#define MMA7660FC_REG_SRST          0x04U
#define MMA7660FC_REG_SPCNT         0x05U
#define MMA7660FC_REG_INTSU         0x06U
#define MMA7660FC_REG_MODE          0x07U
#define MMA7660FC_REG_SR            0x08U
#define MMA7660FC_REG_PDET          0x09U
#define MMA7660FC_REG_PD            0x0AU

/* MODE：0x00 待机，0x01 工作 */
#define MMA7660FC_MODE_STANDBY      0x00U
#define MMA7660FC_MODE_ACTIVE       0x01U
#define MMA7660FC_MODE_BIT_ACTIVE   0x01U

/* SR：AMSR[2:0] 自动唤醒采样率 */
#define MMA7660FC_SR_AMSR_MASK      0x07U
#define MMA7660FC_SR_120HZ          0x00U
#define MMA7660FC_SR_64HZ           0x01U
#define MMA7660FC_SR_32HZ           0x02U
#define MMA7660FC_SR_16HZ           0x03U
#define MMA7660FC_SR_8HZ            0x04U
#define MMA7660FC_SR_4HZ            0x05U
#define MMA7660FC_SR_2HZ            0x06U
#define MMA7660FC_SR_1HZ            0x07U

/* 输出寄存器：bit5:0 为 6 位补码数据，bit7 为 ALERT（自上次读取后已更新） */
#define MMA7660FC_DATA_MASK         0x3FU
#define MMA7660FC_DATA_SIGN         0x20U
#define MMA7660FC_DATA_ALERT        0x80U

/* TILT 寄存器位定义（见数据手册 Table 6） */
#define MMA7660FC_TILT_ALERT        0x80U
#define MMA7660FC_TILT_SHAKE        0x40U
#define MMA7660FC_TILT_TAP2         0x20U
#define MMA7660FC_TILT_TAP1         0x10U
#define MMA7660FC_TILT_POLA         0x08U   /* 0 = 左倾，1 = 右倾 */
#define MMA7660FC_TILT_BAT          0x04U   /* 0 = 背面，1 = 正面 */
#define MMA7660FC_TILT_LAD          0x02U   /* 0 = 竖直，1 = 倒置 */

/* 量程与灵敏度：±1.5g，21.33 counts/g */
#define MMA7660FC_RANGE_G           1.5f
#define MMA7660FC_COUNTS_PER_G      21.33f

/* 无 WHO_AM_I：用 PDET 寄存器写入/回读特定值作为在位与写通路自检 */
#define MMA7660FC_VERIFY_PATTERN    0x28U

#define MMA7660FC_MODE_DELAY_MS     2U

#endif /* MMA7660FC_REGISTER_H */
