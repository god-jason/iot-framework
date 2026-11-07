#ifndef AS5600_REGISTER_H
#define AS5600_REGISTER_H

/* AS5600 磁性旋转编码器寄存器与常量（ams，I2C 0x36） */

/* AS5600 地址固定为 0x36，无地址选择引脚 */
#define AS5600_DEFAULT_ADDRESS      0x36U

#define AS5600_REG_ZMCO             0x00U
#define AS5600_REG_ZPOS_H           0x01U
#define AS5600_REG_ZPOS_L           0x02U
#define AS5600_REG_MPOS_H           0x03U
#define AS5600_REG_MPOS_L           0x04U
#define AS5600_REG_MANG_H           0x05U
#define AS5600_REG_MANG_L           0x06U
#define AS5600_REG_CONF_H           0x07U
#define AS5600_REG_CONF_L           0x08U
#define AS5600_REG_STATUS           0x0BU
#define AS5600_REG_RAW_ANGLE_H      0x0CU
#define AS5600_REG_RAW_ANGLE_L      0x0DU
#define AS5600_REG_ANGLE_H          0x0EU
#define AS5600_REG_ANGLE_L          0x0FU
#define AS5600_REG_AGC              0x1AU
#define AS5600_REG_MAGNITUDE_H      0x1BU
#define AS5600_REG_MAGNITUDE_L      0x1CU
#define AS5600_REG_BURN             0xFFU

/* STATUS：磁铁状态位 */
#define AS5600_STATUS_MH            0x20U   /* AGC 达最小增益：磁场过强 */
#define AS5600_STATUS_ML            0x10U   /* AGC 达最大增益：磁场过弱 */
#define AS5600_STATUS_MD            0x08U   /* 检测到磁铁 */

/* CONF 位域（16 位） */
#define AS5600_CONF_PM_MASK         0x0003U /* 电源模式 PM[1:0] */
#define AS5600_CONF_PM_SHIFT        0U
#define AS5600_CONF_HYST_MASK       0x000CU /* 迟滞 HYST[3:2] */
#define AS5600_CONF_HYST_SHIFT      2U
#define AS5600_CONF_OUTS_MASK       0x0030U /* 输出级 OUTS[5:4] */
#define AS5600_CONF_OUTS_SHIFT      4U
#define AS5600_CONF_PWMF_MASK       0x00C0U /* PWM 频率 PWMF[7:6] */
#define AS5600_CONF_PWMF_SHIFT      6U
#define AS5600_CONF_SF_MASK         0x0300U /* 慢滤波 SF[9:8] */
#define AS5600_CONF_SF_SHIFT        8U
#define AS5600_CONF_FTH_MASK        0x1C00U /* 快滤波阈值 FTH[12:10] */
#define AS5600_CONF_FTH_SHIFT       10U
#define AS5600_CONF_WD_MASK         0x2000U /* 看门狗 WD[13] */

/* 电源模式 */
#define AS5600_PM_NOMINAL           0x00U
#define AS5600_PM_LPM1              0x01U
#define AS5600_PM_LPM2              0x02U
#define AS5600_PM_LPM3              0x03U

/* 角度换算：12 位原始值 → 0~360° */
#define AS5600_ANGLE_COUNTS         4096U
#define AS5600_DEG_PER_LSB          (360.0f / (float)AS5600_ANGLE_COUNTS)

/* 无 WHO_AM_I：用 CONF 写入/回读固定值作为在位与写通路自检 */
#define AS5600_CONF_VERIFY_MASK     0x0300U
#define AS5600_CONF_VERIFY_VALUE    0x0100U

#define AS5600_BOOT_DELAY_MS        2U
#define AS5600_ANGLE_BURST_LEN      5U   /* STATUS + RAW(2) + ANGLE(2) */

#endif /* AS5600_REGISTER_H */
