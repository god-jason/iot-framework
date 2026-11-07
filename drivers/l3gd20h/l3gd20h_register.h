#ifndef L3GD20H_REGISTER_H
#define L3GD20H_REGISTER_H

/* L3GD20H 三轴陀螺仪寄存器与常量（ST DS 文档） */

#define L3GD20H_DEFAULT_ADDRESS     0x6AU
#define L3GD20H_ADDRESS_ALT         0x6BU

#define L3GD20H_REG_WHO_AM_I        0x0FU
#define L3GD20H_REG_CTRL1           0x20U
#define L3GD20H_REG_CTRL2           0x21U
#define L3GD20H_REG_CTRL3           0x22U
#define L3GD20H_REG_CTRL4           0x23U
#define L3GD20H_REG_CTRL5           0x24U
#define L3GD20H_REG_REFERENCE       0x25U
#define L3GD20H_REG_OUT_TEMP        0x26U
#define L3GD20H_REG_STATUS          0x27U
#define L3GD20H_REG_OUT_X_L         0x28U
#define L3GD20H_REG_OUT_X_H         0x29U
#define L3GD20H_REG_OUT_Y_L         0x2AU
#define L3GD20H_REG_OUT_Y_H         0x2BU
#define L3GD20H_REG_OUT_Z_L         0x2CU
#define L3GD20H_REG_OUT_Z_H         0x2DU
#define L3GD20H_REG_FIFO_CTRL       0x2EU
#define L3GD20H_REG_FIFO_SRC        0x2FU

#define L3GD20H_WHO_AM_I_VALUE      0xD7U

/* CTRL1：DR[7:6] 输出率，BW[5:4] 低通带宽，PD[3] 正常模式，Zen/Yen/Xen[2:0] */
#define L3GD20H_CTRL1_ODR_SHIFT     6U
#define L3GD20H_CTRL1_BW_SHIFT      4U
#define L3GD20H_CTRL1_POWER_DOWN    0x00U
#define L3GD20H_CTRL1_PD            0x08U
#define L3GD20H_CTRL1_AXES_EN       0x07U
#define L3GD20H_CTRL1_BW_12HZ5      0x00U

/* CTRL4：BDU[7] 块更新，FS[5:4] 量程 */
#define L3GD20H_CTRL4_BDU           0x80U
#define L3GD20H_CTRL4_FS_SHIFT      4U
#define L3GD20H_CTRL4_FS_MASK       0x30U

/* STATUS 状态位 */
#define L3GD20H_STATUS_ZYXOR        0x80U
#define L3GD20H_STATUS_ZOR          0x40U
#define L3GD20H_STATUS_YOR          0x20U
#define L3GD20H_STATUS_XOR          0x10U
#define L3GD20H_STATUS_ZYXDA        0x08U
#define L3GD20H_STATUS_ZDA          0x04U
#define L3GD20H_STATUS_YDA          0x02U
#define L3GD20H_STATUS_XDA          0x01U

/* 总线访问标志 */
#define L3GD20H_SPI_READ            0x80U
#define L3GD20H_SPI_AUTO_INC        0x40U
#define L3GD20H_I2C_AUTO_INC        0x80U
#define L3GD20H_BURST_MAX           8U

/* 灵敏度：mdps/LSB，按量程索引 */
#define L3GD20H_MDPS_PER_LSB_245    8.75f
#define L3GD20H_MDPS_PER_LSB_500    17.50f
#define L3GD20H_MDPS_PER_LSB_2000   70.00f

/* 温度：8 位补码，约 1 °C/LSB，0 对应 25 °C（精度 ±15 °C，仅作参考） */
#define L3GD20H_TEMP_OFFSET_DEGC    25.0f

/* 复位/上电延时 */
#define L3GD20H_BOOT_DELAY_MS       10U
#define L3GD20H_DATA_TIMEOUT_MS     20U

#endif /* L3GD20H_REGISTER_H */
