#ifndef BH1750_REGISTER_H
#define BH1750_REGISTER_H

/* BH1750FVI 环境光传感器命令定义（无寄存器地址） */

#define BH1750_DEFAULT_ADDRESS      0x23U
#define BH1750_ADDRESS_ALT          0x5CU  /* ADDR 引脚接高 */

#define BH1750_CMD_POWER_DOWN       0x00U
#define BH1750_CMD_POWER_ON         0x01U
#define BH1750_CMD_RESET            0x07U

/* 连续测量模式 */
#define BH1750_CMD_CONT_H_RES       0x10U  /* 1 lx 分辨率，120ms */
#define BH1750_CMD_CONT_H_RES2      0x11U  /* 0.5 lx 分辨率，120ms */
#define BH1750_CMD_CONT_L_RES       0x13U  /* 4 lx 分辨率，16ms */

/* 单次测量模式（测量后自动进入掉电） */
#define BH1750_CMD_ONE_H_RES        0x20U
#define BH1750_CMD_ONE_H_RES2       0x21U
#define BH1750_CMD_ONE_L_RES        0x23U

/* 高分辨率模式首次测量时间（ms） */
#define BH1750_MEASURE_DELAY_MS     180U

/* 原始值 -> lux */
#define BH1750_LUX_DIVISOR          1.2f

#endif /* BH1750_REGISTER_H */
