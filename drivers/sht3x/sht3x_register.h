#ifndef SHT3X_REGISTER_H
#define SHT3X_REGISTER_H

/*
 * SHT3x 系列（SHT30/SHT31/SHT33/SHT35）命令与常量。
 *
 * 本驱动是 drivers/sht30 的薄封装：测量命令、转换时序、CRC8(poly 0x31,
 * init 0xFF) 校验全部复用 sht30 的实现，此处不重复定义命令表，
 * 以避免出现两份 CRC 逻辑。
 *
 * 因此本文件只保留与地址/选型相关的常量，并在文件末尾用编译期断言
 * 固化"两侧定义一致"的契约。
 */

#include "sht30/sht30_register.h"

#define SHT3X_DEFAULT_ADDRESS       0x44U   /* ADDR 引脚接 GND */
#define SHT3X_ADDRESS_ALT           0x45U   /* ADDR 引脚接 VDD */

/*
 * 编译期契约检查（C99：负长度数组触发编译错误）。
 * SHT3x 依赖 sht30 的地址、测量命令与 CRC 参数；若有人改动
 * drivers/sht30/sht30_register.h 而本文件未同步，这里会直接编译失败，
 * 而不是让两侧定义悄悄分叉、直到上板才暴露数据错误。
 */
typedef char sht3x_check_addr_default[(SHT3X_DEFAULT_ADDRESS == SHT30_DEFAULT_ADDRESS) ? 1 : -1];
typedef char sht3x_check_addr_alt[(SHT3X_ADDRESS_ALT == SHT30_ADDRESS_ALT) ? 1 : -1];
typedef char sht3x_check_measure_high[(SHT30_CMD_MEASURE_HIGH == 0x2C06U) ? 1 : -1];
typedef char sht3x_check_crc_poly[(SHT30_CRC_POLY == 0x31U) ? 1 : -1];
typedef char sht3x_check_crc_init[(SHT30_CRC_INIT == 0xFFU) ? 1 : -1];

#endif /* SHT3X_REGISTER_H */
