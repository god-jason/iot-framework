#include "iot_endian.h"

uint16_t iot_bswap16(uint16_t value)
{
    return (uint16_t)((value >> 8) | (value << 8));
}

uint32_t iot_bswap32(uint32_t value)
{
    return ((value >> 24) & 0x000000FFu) |
           ((value >> 8)  & 0x0000FF00u) |
           ((value << 8)  & 0x00FF0000u) |
           ((value << 24) & 0xFF000000u);
}

uint64_t iot_bswap64(uint64_t value)
{
    return ((value >> 56) & 0x00000000000000FFULL) |
           ((value >> 40) & 0x000000000000FF00ULL) |
           ((value >> 24) & 0x0000000000FF0000ULL) |
           ((value >> 8)  & 0x00000000FF000000ULL) |
           ((value << 8)  & 0x000000FF00000000ULL) |
           ((value << 24) & 0x0000FF0000000000ULL) |
           ((value << 40) & 0x00FF000000000000ULL) |
           ((value << 56) & 0xFF00000000000000ULL);
}

uint16_t iot_read_le16(const uint8_t *data)
{
    if (data == NULL) {
        return 0;
    }
    return (uint16_t)(data[0] | (data[1] << 8));
}

uint32_t iot_read_le32(const uint8_t *data)
{
    if (data == NULL) {
        return 0;
    }
    return ((uint32_t)data[0]) |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

uint64_t iot_read_le64(const uint8_t *data)
{
    uint64_t v = 0;
    int i;
    if (data == NULL) {
        return 0;
    }
    for (i = 0; i < 8; i++) {
        v |= ((uint64_t)data[i]) << (i * 8);
    }
    return v;
}

uint16_t iot_read_be16(const uint8_t *data)
{
    if (data == NULL) {
        return 0;
    }
    return (uint16_t)((data[0] << 8) | data[1]);
}

uint32_t iot_read_be32(const uint8_t *data)
{
    if (data == NULL) {
        return 0;
    }
    return ((uint32_t)data[0] << 24) |
           ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) |
           ((uint32_t)data[3]);
}

uint64_t iot_read_be64(const uint8_t *data)
{
    uint64_t v = 0;
    int i;
    if (data == NULL) {
        return 0;
    }
    for (i = 0; i < 8; i++) {
        v = (v << 8) | data[i];
    }
    return v;
}

void iot_write_le16(uint8_t *data, uint16_t value)
{
    if (data == NULL) {
        return;
    }
    data[0] = (uint8_t)(value & 0xFF);
    data[1] = (uint8_t)((value >> 8) & 0xFF);
}

void iot_write_le32(uint8_t *data, uint32_t value)
{
    if (data == NULL) {
        return;
    }
    data[0] = (uint8_t)(value & 0xFF);
    data[1] = (uint8_t)((value >> 8) & 0xFF);
    data[2] = (uint8_t)((value >> 16) & 0xFF);
    data[3] = (uint8_t)((value >> 24) & 0xFF);
}

void iot_write_le64(uint8_t *data, uint64_t value)
{
    int i;
    if (data == NULL) {
        return;
    }
    for (i = 0; i < 8; i++) {
        data[i] = (uint8_t)((value >> (i * 8)) & 0xFF);
    }
}

void iot_write_be16(uint8_t *data, uint16_t value)
{
    if (data == NULL) {
        return;
    }
    data[0] = (uint8_t)((value >> 8) & 0xFF);
    data[1] = (uint8_t)(value & 0xFF);
}

void iot_write_be32(uint8_t *data, uint32_t value)
{
    if (data == NULL) {
        return;
    }
    data[0] = (uint8_t)((value >> 24) & 0xFF);
    data[1] = (uint8_t)((value >> 16) & 0xFF);
    data[2] = (uint8_t)((value >> 8) & 0xFF);
    data[3] = (uint8_t)(value & 0xFF);
}

void iot_write_be64(uint8_t *data, uint64_t value)
{
    int i;
    if (data == NULL) {
        return;
    }
    for (i = 0; i < 8; i++) {
        data[i] = (uint8_t)((value >> ((7 - i) * 8)) & 0xFF);
    }
}