/*
 * endian.h - 字节序转换与大小端数值读写接口
 *
 * 所有接口按字节操作，与宿主机字节序无关，也不要求缓冲区对齐；
 * 调用者必须保证读写长度足够（2/4/8 字节）。data 为 NULL 时：
 * 读取函数返回 0，写入函数直接返回。
 */

#ifndef ENDIAN_H
#define ENDIAN_H

/*
 * 大小端转换与读写。多字节数值按大端(be)或小端(le)字节序组装/拆分。
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 16/32/64 位字节序翻转，返回逆序后的值 */
uint16_t bswap16(uint16_t value);
uint32_t bswap32(uint32_t value);
uint64_t bswap64(uint64_t value);

/* 按小端读取多字节数值；data 需至少有 2/4/8 字节，NULL 时返回 0 */
uint16_t read_le16(const uint8_t *data);
uint32_t read_le32(const uint8_t *data);
uint64_t read_le64(const uint8_t *data);

/* 按大端读取多字节数值；data 需至少有 2/4/8 字节，NULL 时返回 0 */
uint16_t read_be16(const uint8_t *data);
uint32_t read_be32(const uint8_t *data);
uint64_t read_be64(const uint8_t *data);

/* 按小端写入多字节数值；data 需至少有 2/4/8 字节，NULL 时无操作 */
void write_le16(uint8_t *data, uint16_t value);
void write_le32(uint8_t *data, uint32_t value);
void write_le64(uint8_t *data, uint64_t value);

/* 按大端写入多字节数值；data 需至少有 2/4/8 字节，NULL 时无操作 */
void write_be16(uint8_t *data, uint16_t value);
void write_be32(uint8_t *data, uint32_t value);
void write_be64(uint8_t *data, uint64_t value);

#ifdef __cplusplus
}
#endif

#endif /* ENDIAN_H */
