#ifndef MAG3110_REGISTER_H
#define MAG3110_REGISTER_H

/* MAG3110 三轴磁力计寄存器与常量（NXP，I2C 0x0E） */

#define MAG3110_DEFAULT_ADDRESS     0x0EU

#define MAG3110_REG_DR_STATUS       0x00U
#define MAG3110_REG_OUT_X_MSB       0x01U
#define MAG3110_REG_OUT_X_LSB       0x02U
#define MAG3110_REG_OUT_Y_MSB       0x03U
#define MAG3110_REG_OUT_Y_LSB       0x04U
#define MAG3110_REG_OUT_Z_MSB       0x05U
#define MAG3110_REG_OUT_Z_LSB       0x06U
#define MAG3110_REG_WHO_AM_I        0x07U
#define MAG3110_REG_SYSMOD          0x08U
#define MAG3110_REG_OFF_X_MSB       0x09U
#define MAG3110_REG_OFF_X_LSB       0x0AU
#define MAG3110_REG_OFF_Y_MSB       0x0BU
#define MAG3110_REG_OFF_Y_LSB       0x0CU
#define MAG3110_REG_OFF_Z_MSB       0x0DU
#define MAG3110_REG_OFF_Z_LSB       0x0EU
#define MAG3110_REG_DIE_TEMP        0x0FU
#define MAG3110_REG_CTRL_REG1       0x10U
#define MAG3110_REG_CTRL_REG2       0x11U

#define MAG3110_WHO_AM_I_VALUE      0xC4U

/* DR_STATUS 位：bit7:4 溢出，bit3:0 数据就绪 */
#define MAG3110_DR_STATUS_ZYXOW     0x80U
#define MAG3110_DR_STATUS_ZOW       0x40U
#define MAG3110_DR_STATUS_YOW       0x20U
#define MAG3110_DR_STATUS_XOW       0x10U
#define MAG3110_DR_STATUS_ZYXDR     0x08U
#define MAG3110_DR_STATUS_ZDR       0x04U
#define MAG3110_DR_STATUS_YDR       0x02U
#define MAG3110_DR_STATUS_XDR       0x01U

/* CTRL_REG1：DR[6:5] 数据率，OS[4:3] 过采样，FR[2] 快读，TM[1] 触发，AC[0] 激活 */
#define MAG3110_CTRL1_DR_SHIFT      5U
#define MAG3110_CTRL1_DR_MASK       0x60U
#define MAG3110_CTRL1_OS_SHIFT      3U
#define MAG3110_CTRL1_OS_MASK       0x18U
#define MAG3110_CTRL1_AC            0x01U

/* CTRL_REG2：bit7 自动磁复位，bit5 原始模式，bit4 磁复位 */
#define MAG3110_CTRL2_AUTO_MRST_EN  0x80U
#define MAG3110_CTRL2_RAW           0x20U
#define MAG3110_CTRL2_MAG_RST       0x10U

/* SYSMOD：bit1:0 当前系统模式（0 待机，1 激活） */
#define MAG3110_SYSMOD_ACTIVE       0x01U

/* 灵敏度：0.1 µT/LSB */
#define MAG3110_UT_PER_LSB          0.1f

/* 片上温度：8 位补码，1 °C/LSB，误差较大，仅参考 */
#define MAG3110_DEGC_PER_LSB        1.0f

#define MAG3110_DATA_BURST_LEN      6U
#define MAG3110_START_DELAY_MS      2U
#define MAG3110_DATA_TIMEOUT_MS     20U

#endif /* MAG3110_REGISTER_H */
