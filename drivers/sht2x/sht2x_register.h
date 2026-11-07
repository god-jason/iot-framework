#ifndef SHT2X_REGISTER_H
#define SHT2X_REGISTER_H

/*
 * SHT2x（SHT20 / SHT21 / SHT25）命令与时序定义。
 *
 * 命令式 I2C：写 1 字节命令 -> 等待转换 -> 读 3 字节（2 字节数据 + CRC8）。
 * 数据 LSB 两位为状态位，换算前必须屏蔽。
 * 与 HTU21D 协议兼容，但最大转换时间按 SHT2x 手册给出（更保守）。
 */

#define SHT2X_DEFAULT_ADDRESS       0x40U
#define SHT2X_ADDRESS_ALT           0x41U

/* 不带 clock stretching（主机自行等待，推荐） */
#define SHT2X_CMD_MEASURE_T         0xF3U
#define SHT2X_CMD_MEASURE_RH        0xF5U

/* 带 clock stretching */
#define SHT2X_CMD_MEASURE_T_HOLD    0xE3U
#define SHT2X_CMD_MEASURE_RH_HOLD   0xE5U

/* 用户寄存器与复位 */
#define SHT2X_CMD_WRITE_USER        0xE6U
#define SHT2X_CMD_READ_USER         0xE7U
#define SHT2X_CMD_SOFT_RESET        0xFEU

/* 用户寄存器：复位默认值 0x02；bit7=0 湿度 12 位，bit0=0 温度 14 位 */
#define SHT2X_USER_REG_DEFAULT      0x02U
#define SHT2X_USER_REG_HEATER       0x04U
#define SHT2X_USER_REG_OTP_RELOAD   0x02U

/* 状态位掩码与 VDD 告警位 */
#define SHT2X_STATUS_MASK           0xFFFCU
#define SHT2X_STATUS_VDDS_LOW       0x0002U

/* 时序（ms）：14 位温度最大 85ms，12 位湿度最大 29ms，复位 15ms */
#define SHT2X_RESET_DELAY_MS        15U
#define SHT2X_TEMP_DELAY_MS         85U
#define SHT2X_HUMIDITY_DELAY_MS     29U

/* 换算：T = -46.85 + 175.72 * raw / 2^16，RH = -6.0 + 125.0 * raw / 2^16 */
#define SHT2X_RAW_MAX               65536.0f
#define SHT2X_TEMP_SCALE            175.72f
#define SHT2X_TEMP_OFFSET           (-46.85f)
#define SHT2X_HUMIDITY_SCALE        125.0f
#define SHT2X_HUMIDITY_OFFSET       (-6.0f)

/* CRC8：poly 0x31, init 0x00 */
#define SHT2X_CRC_POLY              0x31U
#define SHT2X_CRC_INIT              0x00U

#endif /* SHT2X_REGISTER_H */
