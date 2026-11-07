#ifndef TCS34725_REGISTER_H
#define TCS34725_REGISTER_H

/*
 * TCS34725 RGB 颜色传感器寄存器定义（I2C）。
 *
 * 注意：访问任何寄存器时命令字节必须先置位 CMD(0x80)，
 * 连续读多个字节时再置位 AUTO_INC(0x20)。
 */

#define TCS34725_DEFAULT_ADDRESS    0x29U

#define TCS34725_CMD                0x80U   /* 命令位，必须置位 */
#define TCS34725_CMD_AUTO_INC       0x20U   /* 地址自增 */

#define TCS34725_REG_ENABLE         0x00U
#define TCS34725_REG_ATIME          0x01U
#define TCS34725_REG_WTIME          0x03U
#define TCS34725_REG_PERS           0x0CU
#define TCS34725_REG_CONFIG         0x0DU
#define TCS34725_REG_CONTROL        0x0FU
#define TCS34725_REG_ID             0x12U
#define TCS34725_REG_CDATAL         0x14U
#define TCS34725_REG_RDATAL         0x16U
#define TCS34725_REG_GDATAL         0x18U
#define TCS34725_REG_BDATAL         0x1AU

/* ENABLE 位域 */
#define TCS34725_ENABLE_PON         0x01U   /* 上电 */
#define TCS34725_ENABLE_AEN         0x02U   /* RGBC 使能 */
#define TCS34725_ENABLE_WEN         0x08U

/* CONTROL.AGAIN */
#define TCS34725_GAIN_1X_CODE       0x00U
#define TCS34725_GAIN_4X_CODE       0x01U
#define TCS34725_GAIN_16X_CODE      0x02U
#define TCS34725_GAIN_60X_CODE      0x03U

/* 器件 ID */
#define TCS34725_ID_TCS34725        0x44U
#define TCS34725_ID_TCS34727        0x4DU

/* ATIME：积分时间 = (256 - ATIME) * 2.4ms，每个 2.4ms 周期最多 1024 计数 */
#define TCS34725_ATIME_DEFAULT      0xF6U   /* 24ms */
#define TCS34725_ATIME_MAX          0xFFU   /* 2.4ms */
#define TCS34725_ATIME_MIN          0x00U   /* 614.4ms，16 位饱和 */
#define TCS34725_INTEGRATION_STEP_MS 2.4f
#define TCS34725_COUNT_PER_STEP     1024U
#define TCS34725_POWER_ON_DELAY_MS  3U

/*
 * lux 近似系数：常用参考实现（含典型玻璃衰减）给出的加权经验值，
 * 不同光学窗口 / 光源必须重新标定，此处仅作量级估算。
 */
#define TCS34725_LUX_COEF_R         (-0.32466f)
#define TCS34725_LUX_COEF_G         (1.57837f)
#define TCS34725_LUX_COEF_B         (-0.73191f)
/* 上述系数对应的归一化基准积分时间（ms） */
#define TCS34725_LUX_REF_TIME_MS    153.6f

#endif /* TCS34725_REGISTER_H */
