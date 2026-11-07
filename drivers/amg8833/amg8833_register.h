#ifndef AMG8833_REGISTER_H
#define AMG8833_REGISTER_H

/* AMG8833 8x8 红外热像阵列寄存器与常数定义 */

/* 7 位从机地址（ADDR 引脚决定） */
#define AMG8833_ADDRESS_LOW             0x68U
#define AMG8833_ADDRESS_HIGH            0x69U
#define AMG8833_ADDRESS_DEFAULT         AMG8833_ADDRESS_HIGH

/* 寄存器 */
#define AMG8833_REG_PCLT                0x00U  /* 配置（normal / sleep / standby） */
#define AMG8833_REG_RST                 0x01U  /* 复位标志，写 1 清零 */
#define AMG8833_REG_FPSC                0x02U  /* 帧率 */
#define AMG8833_REG_INTC                0x03U  /* 中断控制 */
#define AMG8833_REG_STAT                0x04U  /* 状态，写 1 清零 */
#define AMG8833_REG_SCLR                0x05U  /* 状态清除 */
#define AMG8833_REG_AVE                 0x07U  /* 移动平均输出 */
#define AMG8833_REG_INTPOL              0x0AU  /* 中断引脚极性 */
#define AMG8833_REG_TTH                 0x0BU  /* 上限阈值（11 位有符号，0x0B..0x0C 小端） */
#define AMG8833_REG_TTL                 0x0DU  /* 下限阈值（8 位，1℃/LSB） */
#define AMG8833_REG_THML                0x0EU  /* 热敏电阻低字节 */
#define AMG8833_REG_THMH                0x0FU  /* 热敏电阻高字节 */
#define AMG8833_REG_PIXEL_BASE          0x80U  /* 第 1 个像素低字节，共 64x2 字节 */

/* PCLT 取值 */
#define AMG8833_PCLT_NORMAL             0x00U
#define AMG8833_PCLT_SLEEP              0x10U
#define AMG8833_PCLT_STANDBY_60S        0x20U
#define AMG8833_PCLT_STANDBY_10S        0x21U

/* FPSC 取值 */
#define AMG8833_FPSC_10FPS              0x00U
#define AMG8833_FPSC_1FPS               0x01U

/* INTC / STAT 位 */
#define AMG8833_STAT_INT                0x01U
#define AMG8833_STAT_OVF                0x02U
#define AMG8833_STAT_OVF_THM            0x04U
#define AMG8833_INTC_INTEN              0x01U  /* 中断使能 */
#define AMG8833_INTC_INTMODE            0x02U  /* 1: 绝对阈值，0: 差值 */
#define AMG8833_INTC_INTHL              0x04U  /* 1: 高有效 */
#define AMG8833_RST_INITIAL             0x3FU  /* 写 0x3F 清除全部标志 */

/* 分辨率 */
#define AMG8833_PIXEL_COUNT             64U
#define AMG8833_PIXEL_LSB_C             0.25f   /* 12 位有符号 */
#define AMG8833_THERM_LSB_C             0.0625f /* 12 位有符号 */
#define AMG8833_PIXEL_SIGN             0x0800
#define AMG8833_PIXEL_OVF              0x1000
#define AMG8833_TEMP_MIN_C             (-20.0f)
#define AMG8833_TEMP_MAX_C             80.0f

/* 初始化延时（ms） */
#define AMG8833_RESET_DELAY_MS         50U
#define AMG8833_FRAME_DELAY_10FPS_MS   110U

#endif /* AMG8833_REGISTER_H */
