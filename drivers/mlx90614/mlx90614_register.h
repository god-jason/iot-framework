#ifndef MLX90614_REGISTER_H
#define MLX90614_REGISTER_H

/* Melexis MLX90614 红外测温（SMBus / I2C）命令与常数 */

/* 7 位从机地址：出厂默认 0x5A，可改到 0x5B..0x5F */
#define MLX90614_ADDRESS_DEFAULT        0x5AU
#define MLX90614_ADDRESS_MIN            0x5AU
#define MLX90614_ADDRESS_MAX            0x5FU

/* RAM 命令（读） */
#define MLX90614_CMD_RAWIR1             0x04U  /* 原始 IR 通道 1 */
#define MLX90614_CMD_RAWIR2             0x05U  /* 原始 IR 通道 2 */
#define MLX90614_CMD_TA                 0x06U  /* 环境温度 */
#define MLX90614_CMD_TOBJ1              0x07U  /* 目标温度 1 */
#define MLX90614_CMD_TOBJ2              0x08U  /* 目标温度 2 */

/* EEPROM 命令（读/写） */
#define MLX90614_CMD_TOMAX              0x20U
#define MLX90614_CMD_TOMIN              0x21U
#define MLX90614_CMD_PWMCTRL            0x22U
#define MLX90614_CMD_TARANGE            0x23U
#define MLX90614_CMD_EMISSIVITY         0x24U  /* 发射率（默认 0xFFFF = 1.0） */
#define MLX90614_CMD_CONFIG             0x25U  /* 配置寄存器（分辨率/滤波/SMBus 地址） */

/* 特殊命令 */
#define MLX90614_CMD_FLAGS              0xF0U  /* 读标志（需按 datasheet 确认） */
#define MLX90614_CMD_SLEEP              0xFFU  /* 休眠：需保持 SCL 低，由调用方实现 */

/* 换算与 CRC */
#define MLX90614_TEMP_LSB_C             0.02f
#define MLX90614_KELVIN_OFFSET          273.15f
#define MLX90614_RAW_INVALID            0x0000U /* 原始读数错误标志位见 datasheet */
#define MLX90614_CRC_POLY               0x07U
#define MLX90614_CRC_INIT               0x00U

/* 时序（ms） */
#define MLX90614_EEPROM_WRITE_MS        10U
#define MLX90614_EEPROM_ERASE_MS        10U
#define MLX90614_POWER_ON_MS            1U

#endif /* MLX90614_REGISTER_H */
