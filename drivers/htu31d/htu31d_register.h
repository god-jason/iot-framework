#ifndef HTU31D_REGISTER_H
#define HTU31D_REGISTER_H

/*
 * HTU31D 命令与时序定义（命令式 I2C，无寄存器地址）。
 *
 * 与 HTU21D 的区别：命令码和换算公式不同。
 * 每次转换完成后读回 3 字节（2 字节数据 + 1 字节 CRC8）。
 *
 * 说明：命令码依据 TE HTU31D 数据手册的 I2C 命令表；
 *       不同批次的模组固件如返回 NACK，请核对手册出版号。
 */

#define HTU31D_DEFAULT_ADDRESS          0x40U
#define HTU31D_ADDRESS_ALT              0x41U

/* 转换命令（ température first / humidity first ），非 clock stretching 模式 */
#define HTU31D_CMD_CONVERT_TEMP         0x40U
#define HTU31D_CMD_CONVERT_HUMIDITY     0x41U

/* 标识与诊断 */
#define HTU31D_CMD_READ_MANUFACTURER_ID 0x1CU
#define HTU31D_CMD_READ_SERIAL          0x1EU
#define HTU31D_CMD_READ_DIAGNOSTIC      0x1BU

/* 出厂标识值 */
#define HTU31D_MANUFACTURER_ID          0x0008U

/* 时序（ms）：14 位转换典型 3.5ms，留足余量 */
#define HTU31D_CONVERT_DELAY_MS         15U
#define HTU31D_RESET_DELAY_MS           5U

/* 换算：T = -40 + 165 * raw / 65535，RH = 100 * raw / 65535 */
#define HTU31D_RAW_MAX                  65535.0f
#define HTU31D_TEMP_SCALE               165.0f
#define HTU31D_TEMP_OFFSET              (-40.0f)
#define HTU31D_HUMIDITY_SCALE           100.0f

/* CRC8：poly 0x31, init 0xFF */
#define HTU31D_CRC_POLY                 0x31U
#define HTU31D_CRC_INIT                 0xFFU

#endif /* HTU31D_REGISTER_H */
