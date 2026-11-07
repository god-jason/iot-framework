#ifndef PCA9685_REGISTER_H
#define PCA9685_REGISTER_H

/* PCA9685 16 通道 12 位 PWM 控制器寄存器与位定义（I2C，地址 0x40~0x7F） */

#define PCA9685_DEFAULT_ADDRESS     0x40U
#define PCA9685_ADDRESS_MAX         0x7FU

/* 寄存器地址 */
#define PCA9685_REG_MODE1           0x00U
#define PCA9685_REG_MODE2           0x01U
#define PCA9685_REG_SUBADR1         0x02U
#define PCA9685_REG_SUBADR2         0x03U
#define PCA9685_REG_SUBADR3         0x04U
#define PCA9685_REG_ALLCALLADR      0x05U
#define PCA9685_REG_LED0_ON_L       0x06U   /* 通道 n 基址：0x06 + 4n */
#define PCA9685_REG_ALL_LED_ON_L    0xFAU
#define PCA9685_REG_ALL_LED_ON_H    0xFBU
#define PCA9685_REG_ALL_LED_OFF_L   0xFCU
#define PCA9685_REG_ALL_LED_OFF_H   0xFDU
#define PCA9685_REG_PRESCALE        0xFEU
#define PCA9685_REG_TESTMODE        0xFFU

/* MODE1 位 */
#define PCA9685_MODE1_RESTART       0x80U
#define PCA9685_MODE1_EXTCLK        0x40U
#define PCA9685_MODE1_AI            0x20U   /* 地址自动递增 */
#define PCA9685_MODE1_SLEEP         0x10U
#define PCA9685_MODE1_SUB1          0x08U
#define PCA9685_MODE1_SUB2          0x04U
#define PCA9685_MODE1_SUB3          0x02U
#define PCA9685_MODE1_ALLCALL       0x01U

/* MODE2 位 */
#define PCA9685_MODE2_INVRT         0x10U
#define PCA9685_MODE2_OCH           0x08U
#define PCA9685_MODE2_OUTDRV        0x04U   /* 1: 推挽输出 */

/* LEDn_OFF_H 的 bit4：全关（输出恒低）；LEDn_ON_H 的 bit4：全开 */
#define PCA9685_FULL_BIT            0x10U
#define PCA9685_CNT_MASK            0x0FFFU

/* 通道与分辨率 */
#define PCA9685_CHANNEL_COUNT       16U
#define PCA9685_RESOLUTION          4096U
#define PCA9685_FULL_SCALE          4095U

/* 内部振荡器 25MHz，PWM 频率 = 25MHz / (4096 × (prescale + 1)) */
#define PCA9685_OSC_HZ              25000000UL
#define PCA9685_PRESCALE_MIN        1U
#define PCA9685_PRESCALE_MAX        255U

/* 复位默认 prescale 为 30 → 约 200Hz */
#define PCA9685_DEFAULT_FREQ_HZ     200.0f

/* 时序（ms） */
#define PCA9685_SLEEP_DELAY_MS      1U
#define PCA9685_WAKE_DELAY_MS       1U

/* 默认输出配置 */
#define PCA9685_MODE1_DEFAULT       (PCA9685_MODE1_AI | PCA9685_MODE1_ALLCALL)
#define PCA9685_MODE2_DEFAULT       PCA9685_MODE2_OUTDRV

#endif /* PCA9685_REGISTER_H */
