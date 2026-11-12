#ifndef HAL_UTIL_H
#define HAL_UTIL_H

/*
 * 通用工具：校验与字节序转换。
 *
 * 与具体外设无关，供驱动统一复用，避免各驱动重复实现。
 */

#include "hal_def.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 通用 CRC8（poly 由调用方给出） */
static inline uint8_t hal_crc8(const uint8_t *data, size_t len,
                               uint8_t poly, uint8_t init)
{
    uint8_t crc = init;
    size_t i;
    uint8_t b;

    if (data == NULL) {
        return crc;
    }
    for (i = 0U; i < len; i++) {
        crc ^= data[i];
        for (b = 0U; b < 8U; b++) {
            crc = ((crc & 0x80U) != 0U) ? (uint8_t)((crc << 1) ^ poly)
                                        : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

/* Dallas/Maxim CRC8：poly=0x31, init=0x00, 反射算法 */
static inline uint8_t hal_crc8_maxim(const uint8_t *data, size_t len)
{
    uint8_t crc = 0x00U;
    size_t i;
    uint8_t b;

    if (data == NULL) {
        return crc;
    }
    for (i = 0U; i < len; i++) {
        crc ^= data[i];
        for (b = 0U; b < 8U; b++) {
            crc = ((crc & 0x01U) != 0U) ? (uint8_t)((crc >> 1) ^ 0x8CU)
                                        : (uint8_t)(crc >> 1);
        }
    }
    return crc;
}

/* CRC16/Modbus */
static inline uint16_t hal_crc16_modbus(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFFU;
    size_t i;
    uint8_t b;

    if (data == NULL) {
        return crc;
    }
    for (i = 0U; i < len; i++) {
        crc ^= (uint16_t)data[i];
        for (b = 0U; b < 8U; b++) {
            crc = ((crc & 0x0001U) != 0U) ? (uint16_t)((crc >> 1) ^ 0xA001U)
                                          : (uint16_t)(crc >> 1);
        }
    }
    return crc;
}

/* 大端/小端 16 位（返回有符号，便于温度等值的直接换算） */
static inline int16_t hal_be16(const uint8_t *p)
{
    return (int16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

static inline int16_t hal_le16(const uint8_t *p)
{
    return (int16_t)(((uint16_t)p[1] << 8) | (uint16_t)p[0]);
}

static inline int32_t hal_be24(const uint8_t *p)
{
    int32_t v = ((int32_t)p[0] << 16) | ((int32_t)p[1] << 8) | (int32_t)p[2];

    if ((v & 0x800000) != 0) {
        v |= (int32_t)0xFF000000;
    }
    return v;
}

#ifdef __cplusplus
}
#endif

#endif /* HAL_UTIL_H */
