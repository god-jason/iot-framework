#include "iot_modbus.h"
#include "iot_crc.h"
#include "iot_endian.h"
#include "iot_memory.h"

int iot_modbus_init(void)
{
    return IOT_OK;
}

uint16_t iot_modbus_crc16(const uint8_t *data, uint32_t len)
{
    return iot_crc16_modbus(data, len);
}

/* 追加 CRC16（低字节在前） */
static int frame_append_crc(uint8_t *out, uint32_t *len, uint32_t capacity)
{
    uint16_t crc;

    if (*len + 2 > capacity) {
        return IOT_ERR_OVERFLOW;
    }
    crc = iot_crc16_modbus(out, *len);
    out[(*len)++] = (uint8_t)(crc & 0xFF);
    out[(*len)++] = (uint8_t)(crc >> 8);
    return IOT_OK;
}

int iot_modbus_build_read_regs(uint8_t slave, uint16_t addr, uint16_t qty,
                               uint8_t *out, uint32_t capacity, uint32_t *len)
{
    if (out == NULL || len == NULL || capacity < 8) {
        return IOT_ERR_PARAM;
    }
    *len = 0;
    out[(*len)++] = slave;
    out[(*len)++] = 0x03;
    iot_write_be16(&out[*len], addr); *len += 2;
    iot_write_be16(&out[*len], qty);  *len += 2;
    return frame_append_crc(out, len, capacity);
}

int iot_modbus_build_write_reg(uint8_t slave, uint16_t addr, uint16_t value,
                               uint8_t *out, uint32_t capacity, uint32_t *len)
{
    if (out == NULL || len == NULL || capacity < 8) {
        return IOT_ERR_PARAM;
    }
    *len = 0;
    out[(*len)++] = slave;
    out[(*len)++] = 0x06;
    iot_write_be16(&out[*len], addr);  *len += 2;
    iot_write_be16(&out[*len], value); *len += 2;
    return frame_append_crc(out, len, capacity);
}

int iot_modbus_build_write_regs(uint8_t slave, uint16_t addr,
                                const uint16_t *regs, uint16_t count,
                                uint8_t *out, uint32_t capacity, uint32_t *len)
{
    uint16_t i;

    if (out == NULL || len == NULL || regs == NULL || count == 0) {
        return IOT_ERR_PARAM;
    }
    if (capacity < (uint32_t)(9 + count * 2)) {
        return IOT_ERR_OVERFLOW;
    }
    *len = 0;
    out[(*len)++] = slave;
    out[(*len)++] = 0x10;
    iot_write_be16(&out[*len], addr); *len += 2;
    iot_write_be16(&out[*len], count); *len += 2;
    out[(*len)++] = (uint8_t)(count * 2);   /* 字节数 */
    for (i = 0; i < count; i++) {
        iot_write_be16(&out[*len], regs[i]);
        *len += 2;
    }
    return frame_append_crc(out, len, capacity);
}

int iot_modbus_decode(const uint8_t *frame, uint32_t len,
                      iot_modbus_request_t *req, const uint8_t **data, uint32_t *data_len)
{
    uint16_t crc, calc;

    if (frame == NULL || len < 4 || req == NULL) {
        return IOT_ERR_PARAM;
    }
    crc = (uint16_t)(frame[len - 1] << 8) | frame[len - 2];
    calc = iot_crc16_modbus(frame, len - 2);
    if (crc != calc) {
        return IOT_ERR_CRC;
    }

    req->slave_id = frame[0];
    req->function = frame[1];

    if (data != NULL && data_len != NULL) {
        *data = &frame[2];
        *data_len = len - 4;   /* 去掉 slave/function/CRC */
    }
    return IOT_OK;
}