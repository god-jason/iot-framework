#ifndef CRC16_H
#define CRC16_H

/*
 * crc16.h - CRC-16 校验（MODBUS 与 CCITT-FALSE 两种标准配置）
 *
 * 无状态纯函数，可重入；len 为 0 时 data 可为 NULL。
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* CRC-16/MODBUS：poly 0x8005，init 0xFFFF，输入反射，xorout 0x0000 */
uint16_t crc16_modbus(const void *data, uint32_t len);

/* CRC-16/CCITT-FALSE：poly 0x1021，init 0xFFFF，不反射，xorout 0x0000 */
uint16_t crc16_ccitt(const void *data, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* CRC16_H */
