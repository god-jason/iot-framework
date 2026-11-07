#ifndef CRC_H
#define CRC_H

/*
 * CRC 校验。命名函数采用广泛使用的标准配置：
 *   crc8          -> CRC-8 (poly 0x07, init 0x00)
 *   crc16_modbus  -> CRC-16/MODBUS (poly 0x8005, init 0xFFFF, 反射)
 *   crc16_ccitt   -> CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF)
 *   crc32         -> CRC-32/IEEE 802.3 (poly 0x04C11DB7, init/xorout 0xFFFFFFFF)
 * 固件完整性校验建议使用 crc32；需要更多变体时可用 crc_calculate()。
 */

#include "iot_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t polynomial;   /* 生成多项式 */
    uint32_t initial;      /* 初始值 */
    uint32_t xor_out;      /* 输出异或值 */
    uint8_t  width;        /* 位宽：8 / 16 / 32 */
    bool     reflect_in;   /* 输入字节是否按位反转（反射算法） */
    bool     reflect_out;  /* 输出是否再按位反转 */
} crc_config_t;

uint8_t  crc8(const void *data, uint32_t len);
uint16_t crc16_modbus(const void *data, uint32_t len);
uint16_t crc16_ccitt(const void *data, uint32_t len);
uint32_t crc32(const void *data, uint32_t len);

uint32_t crc_calculate(const crc_config_t *config,
                           const void *data,
                           uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* CRC_H */