#ifndef CRC8_H
#define CRC8_H

/*
 * crc8.h - CRC-8 校验（poly 0x07，init 0x00，不反射，xorout 0x00）
 *
 * 无状态纯函数，可重入；len 为 0 时 data 可为 NULL。
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 计算 CRC-8；data 非法（NULL 且 len != 0）时返回 0 */
uint8_t crc8(const void *data, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* CRC8_H */
