#ifndef SHT4X_REGISTER_H
#define SHT4X_REGISTER_H

/*
 * SHT4x（SHT40 / SHT41 / SHT45）命令定义。
 *
 * 与 SHT3x 不同：命令只有 1 字节，且没有 CRC 之外的参数；测量命令本身
 * 就编码了重复性，读回固定 6 字节：
 *   [0..1] 温度原始值  [2] CRC   [3..4] 湿度原始值  [5] CRC
 */

#define SHT4X_DEFAULT_ADDRESS       0x44U
#define SHT4X_ADDRESS_ALT           0x45U

/* 测量命令（单次转换，无 clock stretching） */
#define SHT4X_CMD_MEASURE_HIGH      0xFDU   /* 高重复性，约 8.3ms */
#define SHT4X_CMD_MEASURE_MEDIUM    0xF6U   /* 中重复性，约 4.5ms */
#define SHT4X_CMD_MEASURE_LOW       0xE0U   /* 低重复性，约 1.6ms */

/* 其它命令 */
#define SHT4X_CMD_READ_SERIAL       0x89U   /* 返回 2 字节 SN + CRC */
#define SHT4X_CMD_SOFT_RESET        0x94U

/* 时序（ms），按最坏情况留余量 */
#define SHT4X_RESET_DELAY_MS        2U
#define SHT4X_MEASURE_DELAY_MS      10U
#define SHT4X_MEASURE_DELAY_MED_MS  6U
#define SHT4X_MEASURE_DELAY_LOW_MS  3U

/* 换算：T = -45 + 175 * raw / 65535，RH = -6 + 125 * raw / 65535 */
#define SHT4X_RAW_MAX               65535.0f
#define SHT4X_TEMP_SCALE            175.0f
#define SHT4X_TEMP_OFFSET           (-45.0f)
#define SHT4X_HUMIDITY_SCALE        125.0f
#define SHT4X_HUMIDITY_OFFSET       (-6.0f)

/* CRC8：poly 0x31, init 0xFF */
#define SHT4X_CRC_POLY              0x31U
#define SHT4X_CRC_INIT              0xFFU

#endif /* SHT4X_REGISTER_H */
