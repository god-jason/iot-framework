#include "iot_crc.h"

static uint32_t crc_reflect(uint32_t value, uint8_t width)
{
    uint32_t r = 0;
    uint8_t i;
    for (i = 0; i < width; i++) {
        if (value & (1u << i)) {
            r |= (1u << (width - 1 - i));
        }
    }
    return r;
}

uint32_t iot_crc_calculate(const iot_crc_config_t *config,
                           const void *data,
                           uint32_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t crc, mask, poly;
    uint32_t i;

    if (config == NULL || (data == NULL && len != 0)) {
        return 0;
    }
    if (config->width > 32 || config->width == 0) {
        return 0;
    }

    mask = (config->width >= 32) ? 0xFFFFFFFFu : ((1u << config->width) - 1u);
    crc = config->initial & mask;

    if (config->reflect_in) {
        /* 反射算法：LSB 优先 */
        poly = crc_reflect(config->polynomial, config->width) & mask;
        for (i = 0; i < len; i++) {
            uint8_t bit;
            crc ^= p[i];
            for (bit = 0; bit < 8; bit++) {
                if (crc & 1u) {
                    crc = (crc >> 1) ^ poly;
                } else {
                    crc = (crc >> 1);
                }
            }
            crc &= mask;
        }
    } else {
        /* 常规算法：MSB 优先 */
        uint32_t topbit = 1u << (config->width - 1);
        poly = config->polynomial & mask;
        for (i = 0; i < len; i++) {
            uint8_t bit;
            crc ^= ((uint32_t)p[i]) << (config->width - 8);
            for (bit = 0; bit < 8; bit++) {
                if (crc & topbit) {
                    crc = (crc << 1) ^ poly;
                } else {
                    crc = (crc << 1);
                }
            }
            crc &= mask;
        }
    }

    if (config->reflect_out) {
        crc = crc_reflect(crc, config->width);
    }

    return (crc ^ config->xor_out) & mask;
}

uint8_t iot_crc8(const void *data, uint32_t len)
{
    iot_crc_config_t cfg;
    cfg.polynomial = 0x07u;
    cfg.initial    = 0x00u;
    cfg.xor_out    = 0x00u;
    cfg.width      = 8;
    cfg.reflect_in = false;
    cfg.reflect_out = false;
    return (uint8_t)iot_crc_calculate(&cfg, data, len);
}

uint16_t iot_crc16_modbus(const void *data, uint32_t len)
{
    iot_crc_config_t cfg;
    cfg.polynomial = 0x8005u;
    cfg.initial    = 0xFFFFu;
    cfg.xor_out    = 0x0000u;
    cfg.width      = 16;
    cfg.reflect_in = true;
    cfg.reflect_out = false;
    return (uint16_t)iot_crc_calculate(&cfg, data, len);
}

uint16_t iot_crc16_ccitt(const void *data, uint32_t len)
{
    iot_crc_config_t cfg;
    cfg.polynomial = 0x1021u;
    cfg.initial    = 0xFFFFu;
    cfg.xor_out    = 0x0000u;
    cfg.width      = 16;
    cfg.reflect_in = false;
    cfg.reflect_out = false;
    return (uint16_t)iot_crc_calculate(&cfg, data, len);
}

uint32_t iot_crc32(const void *data, uint32_t len)
{
    iot_crc_config_t cfg;
    cfg.polynomial = 0x04C11DB7u;
    cfg.initial    = 0xFFFFFFFFu;
    cfg.xor_out    = 0xFFFFFFFFu;
    cfg.width      = 32;
    cfg.reflect_in = true;
    cfg.reflect_out = false;
    return iot_crc_calculate(&cfg, data, len);
}