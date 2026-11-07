#ifndef APDS9960_REGISTER_H
#define APDS9960_REGISTER_H

/* APDS-9960 接近 / RGB 颜色 / 手势传感器寄存器定义（I2C，8 位寄存器） */

#define APDS9960_DEFAULT_ADDRESS    0x39U

#define APDS9960_REG_ENABLE         0x80U
#define APDS9960_REG_ATIME          0x81U
#define APDS9960_REG_WTIME          0x83U
#define APDS9960_REG_PPULSE         0x8EU
#define APDS9960_REG_CONTROL        0x8FU
#define APDS9960_REG_CONFIG2        0x90U
#define APDS9960_REG_ID             0x92U
#define APDS9960_REG_STATUS         0x93U
#define APDS9960_REG_CDATAL         0x94U
#define APDS9960_REG_RDATAL         0x96U
#define APDS9960_REG_GDATAL         0x98U
#define APDS9960_REG_BDATAL         0x9AU
#define APDS9960_REG_PDATA          0x9CU
#define APDS9960_REG_CONFIG3        0x9FU

/* ENABLE 位域 */
#define APDS9960_ENABLE_PON         0x01U
#define APDS9960_ENABLE_AEN         0x02U
#define APDS9960_ENABLE_PEN         0x04U
#define APDS9960_ENABLE_WEN         0x08U
#define APDS9960_ENABLE_AIEN        0x10U
#define APDS9960_ENABLE_PIEN        0x20U
#define APDS9960_ENABLE_GEN         0x40U

/* STATUS 位域 */
#define APDS9960_STATUS_AVALID      0x01U
#define APDS9960_STATUS_PVALID      0x02U

/* CONTROL.AGAIN（ALS 增益，bits 1:0） */
#define APDS9960_AGAIN_CODE_1X      0x00U
#define APDS9960_AGAIN_CODE_4X      0x01U
#define APDS9960_AGAIN_CODE_16X     0x02U
#define APDS9960_AGAIN_CODE_64X     0x03U
/* CONTROL.PGAIN（接近增益，bits 3:2） */
#define APDS9960_PGAIN_1X           (0x00U << 2)
#define APDS9960_PGAIN_2X           (0x01U << 2)
#define APDS9960_PGAIN_4X           (0x02U << 2)
#define APDS9960_PGAIN_8X           (0x03U << 2)

/* PPULSE：bits 7:6 脉宽，bits 5:0 脉冲数（1~64） */
#define APDS9960_PPULSE_LEN_4US     0x00U
#define APDS9960_PPULSE_LEN_8US     0x40U
#define APDS9960_PPULSE_LEN_16US    0x80U
#define APDS9960_PPULSE_LEN_32US    0xC0U
#define APDS9960_PPULSE_COUNT_MASK  0x3FU

/* 器件 ID：APDS-9960 固定为 0xA8 */
#define APDS9960_ID_APDS9960        0xA8U

/* ALS 积分时间 = (256 - ATIME) * 2.78ms */
#define APDS9960_ATIME_DEFAULT      0xF6U   /* ~27.8ms */
#define APDS9960_ALS_STEP_MS        2.78f
#define APDS9960_POWER_ON_DELAY_MS  10U
/* 状态标志轮询超时（ms） */
#define APDS9960_POLL_TIMEOUT_MS    100U

#endif /* APDS9960_REGISTER_H */
