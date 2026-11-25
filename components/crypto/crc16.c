/*
 * crc16.c - CRC-16 校验（MODBUS 与 CCITT-FALSE）
 *
 * 逐字节、逐位计算，不占用查表空间。
 * 标准校验串 "123456789"：MODBUS -> 0x4B37，CCITT-FALSE -> 0x29B1。
 */

#include "crc16.h"

#include <stddef.h>

uint16_t crc16_modbus(const void *data, uint32_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint16_t crc = 0xFFFFU;
    uint32_t i;
    uint8_t bit;

    if (data == NULL && len != 0U) {
        return 0U;
    }

    for (i = 0U; i < len; i++) {
        crc ^= p[i];
        for (bit = 0U; bit < 8U; bit++) {
            if ((crc & 0x0001U) != 0U) {
                crc = (uint16_t)((crc >> 1) ^ 0xA001U);   /* 0x8005 的镜像 */
            } else {
                crc = (uint16_t)(crc >> 1);
            }
        }
    }
    return crc;
}

uint16_t crc16_ccitt(const void *data, uint32_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint16_t crc = 0xFFFFU;
    uint32_t i;
    uint8_t bit;

    if (data == NULL && len != 0U) {
        return 0U;
    }

    for (i = 0U; i < len; i++) {
        crc ^= (uint16_t)((uint16_t)p[i] << 8);
        for (bit = 0U; bit < 8U; bit++) {
            if ((crc & 0x8000U) != 0U) {
                crc = (uint16_t)((crc << 1) ^ 0x1021U);
            } else {
                crc = (uint16_t)(crc << 1);
            }
        }
    }
    return crc;
}
