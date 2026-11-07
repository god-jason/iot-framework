#ifndef IOT_ENDIAN_H
#define IOT_ENDIAN_H

/*
 * 大小端转换与读写。多字节数值按大端(be)或小端(le)字节序组装/拆分。
 */

#include "iot_types.h"

#ifdef __cplusplus
extern "C" {
#endif

uint16_t iot_bswap16(uint16_t value);
uint32_t iot_bswap32(uint32_t value);
uint64_t iot_bswap64(uint64_t value);

uint16_t iot_read_le16(const uint8_t *data);
uint32_t iot_read_le32(const uint8_t *data);
uint64_t iot_read_le64(const uint8_t *data);

uint16_t iot_read_be16(const uint8_t *data);
uint32_t iot_read_be32(const uint8_t *data);
uint64_t iot_read_be64(const uint8_t *data);

void iot_write_le16(uint8_t *data, uint16_t value);
void iot_write_le32(uint8_t *data, uint32_t value);
void iot_write_le64(uint8_t *data, uint64_t value);

void iot_write_be16(uint8_t *data, uint16_t value);
void iot_write_be32(uint8_t *data, uint32_t value);
void iot_write_be64(uint8_t *data, uint64_t value);

#ifdef __cplusplus
}
#endif

#endif /* IOT_ENDIAN_H */