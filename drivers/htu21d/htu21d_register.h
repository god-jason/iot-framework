#ifndef HTU21D_REGISTER_H
#define HTU21D_REGISTER_H

/*
 * HTU21D / HTU21D(F) 命令与时序定义。
 *
 * 器件为命令式 I2C 接口（无寄存器地址）：
 *   写 1 字节命令 -> 等待转换 -> 读回 3 字节（2 字节数据 + 1 字节 CRC8）
 * 数据 LSB 两位为状态位，必须屏蔽后再换算。
 */

#define HTU21D_DEFAULT_ADDRESS      0x40U
#define HTU21D_ADDRESS_ALT          0x41U

/* 测量命令：带 clock stretching（HOLD）版本 */
#define HTU21D_CMD_MEASURE_T_HOLD   0xE3U
#define HTU21D_CMD_MEASURE_RH_HOLD  0xE5U

/* 测量命令：不带 clock stretching（推荐，主机自行等待） */
#define HTU21D_CMD_MEASURE_T        0xF3U
#define HTU21D_CMD_MEASURE_RH       0xF5U

/* 用户寄存器与复位 */
#define HTU21D_CMD_WRITE_USER       0xE6U
#define HTU21D_CMD_READ_USER        0xE7U
#define HTU21D_CMD_SOFT_RESET       0xFEU

/* 用户寄存器默认值（复位后 bit1 恒为 1） */
#define HTU21D_USER_REG_DEFAULT     0x02U
/* 默认分辨率：温度 14 位、湿度 12 位（bit7=0, bit0=0） */
#define HTU21D_USER_REG_RES_MASK    0x81U
#define HTU21D_USER_REG_HEATER      0x04U

/* 状态位：数据 LSB 两位，bit1 为 VDD 低压告警 */
#define HTU21D_STATUS_MASK          0xFFFCU
#define HTU21D_STATUS_VDDS_LOW      0x0002U

/* 时序（ms）：14 位温度最大 50ms，12 位湿度最大 16ms，复位 15ms */
#define HTU21D_RESET_DELAY_MS       15U
#define HTU21D_TEMP_DELAY_MS        50U
#define HTU21D_HUMIDITY_DELAY_MS    16U

/* 换算系数：T = -46.85 + 175.72 * raw / 2^16，RH = -6.0 + 125.0 * raw / 2^16 */
#define HTU21D_RAW_MAX              65536.0f
#define HTU21D_TEMP_SCALE           175.72f
#define HTU21D_TEMP_OFFSET          (-46.85f)
#define HTU21D_HUMIDITY_SCALE       125.0f
#define HTU21D_HUMIDITY_OFFSET      (-6.0f)

/* CRC8：poly 0x31, init 0x00（与 SHT2x 相同） */
#define HTU21D_CRC_POLY             0x31U
#define HTU21D_CRC_INIT             0x00U

#endif /* HTU21D_REGISTER_H */
