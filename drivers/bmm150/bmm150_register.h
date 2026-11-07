#ifndef BMM150_REGISTER_H
#define BMM150_REGISTER_H

/* BMM150 三轴磁力计寄存器与常量（Bosch，I2C 0x10/0x11 或 SPI） */

#define BMM150_DEFAULT_ADDRESS        0x10U
#define BMM150_ADDRESS_ALT            0x11U

#define BMM150_REG_CHIP_ID            0x00U
#define BMM150_CHIP_ID_VALUE          0x32U

/* 数据寄存器：X/Y 为 13 位，Z 为 15 位，RHALL 为 14 位 */
#define BMM150_REG_DATA_X_LSB         0x42U
#define BMM150_REG_DATA_X_MSB         0x43U
#define BMM150_REG_DATA_Y_LSB         0x44U
#define BMM150_REG_DATA_Y_MSB         0x45U
#define BMM150_REG_DATA_Z_LSB         0x46U
#define BMM150_REG_DATA_Z_MSB         0x47U
#define BMM150_REG_DATA_RHALL_LSB     0x48U
#define BMM150_REG_DATA_RHALL_MSB     0x49U
#define BMM150_DATA_BURST_LEN         8U

#define BMM150_REG_INT_STATUS         0x4AU
#define BMM150_REG_POWER_CONTROL      0x4BU
#define BMM150_REG_OP_MODE            0x4CU
#define BMM150_REG_INT_CONFIG         0x4DU
#define BMM150_REG_AXES_ENABLE        0x4EU
#define BMM150_REG_LOW_THRESHOLD      0x4FU
#define BMM150_REG_HIGH_THRESHOLD     0x50U
#define BMM150_REG_REP_XY             0x51U
#define BMM150_REG_REP_Z              0x52U

/* 出厂 trim 值（本驱动读入后仅保存，未参与换算，见 bmm150_read 注释） */
#define BMM150_REG_TRIM_X1            0x5DU
#define BMM150_REG_TRIM_XYZ1_LSB      0x6BU
#define BMM150_REG_TRIM_XYZ1_MSB      0x6CU
#define BMM150_TRIM_LEN               16U

/* POWER_CONTROL：bit0 上电，软复位写 0x82 */
#define BMM150_POWER_CONTROL_ON       0x01U
#define BMM150_POWER_CONTROL_RESET    0x82U

/* OP_MODE：位 [1:0] 工作模式 */
#define BMM150_OP_MODE_SUSPEND        0x00U
#define BMM150_OP_MODE_SLEEP          0x01U
#define BMM150_OP_MODE_FORCED         0x02U

/* AXES_ENABLE：位为 1 表示禁用对应轴，0x00 = 三轴全开 */
#define BMM150_AXES_ENABLE_ALL        0x00U

/* INT_STATUS：bit0 数据就绪 */
#define BMM150_INT_STATUS_DRDY        0x01U

/*
 * 灵敏度：X/Y 为 13 位，0.3 µT/LSB；Z 为 15 位，满量程 ±2500 µT，
 * 对应 0.15 µT/LSB。Bosch 参考代码使用出厂 trim 做多项式补偿，
 * 本驱动采用标称灵敏度（误差约 ±3%），可用 bmm150_set_calibration 修正偏置。
 */
#define BMM150_UT_PER_LSB_XY          0.3f
#define BMM150_UT_PER_LSB_Z           0.15f

/* 总线标志 */
#define BMM150_SPI_READ               0x80U
#define BMM150_BURST_MAX              8U

/* 延时 */
#define BMM150_RESET_DELAY_MS         2U
#define BMM150_POWER_ON_DELAY_MS      2U
#define BMM150_FORCED_DELAY_MS        10U

#endif /* BMM150_REGISTER_H */
