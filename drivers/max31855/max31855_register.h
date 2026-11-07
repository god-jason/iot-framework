#ifndef MAX31855_REGISTER_H
#define MAX31855_REGISTER_H

/* MAX31855 K 型热电偶转换器（SPI 只读，32 位输出） */

/* 32 位读出格式（bit31 为最先输出的符号位）
 *   bit31 - 18 : 热电偶温度，14 位有符号，1 LSB = 0.25 ℃
 *   bit17      : 保留
 *   bit16      : 故障标志（任一故障置位）
 *   bit15 - 4  : 内部（冷端）温度，12 位有符号，1 LSB = 0.0625 ℃
 *   bit3       : 保留
 *   bit2       : SCV - 热电偶对 VCC 短路
 *   bit1       : SCG - 热电偶对 GND 短路
 *   bit0       : OC  - 热电偶开路
 */

#define MAX31855_TC_SHIFT           18U
#define MAX31855_TC_MASK            0x00001FFFU  /* 故障位（bit16）之外的 14 位 */
#define MAX31855_TC_LSB_CELSIUS     0.25f

#define MAX31855_INTERNAL_SHIFT     4U
#define MAX31855_INTERNAL_MASK      0x00000FFFU
#define MAX31855_INTERNAL_LSB       0.0625f

#define MAX31855_FAULT_BIT          0x00010000U
#define MAX31855_FAULT_OC           0x01U
#define MAX31855_FAULT_SCG          0x02U
#define MAX31855_FAULT_SCV          0x04U

#define MAX31855_FRAME_SIZE         4U
/* 转换时间：典型 70 - 100ms，保守 120ms */
#define MAX31855_CONV_DELAY_MS      120U

#endif /* MAX31855_REGISTER_H */
