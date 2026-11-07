/*
 * Modbus RTU / TCP 主站 + 从站。
 * 帧格式：
 *   RTU: [slave][PDU][crc16_le]
 *   TCP: [tid(2)][pid(2)=0][len(2)=unit+PDU][unit][PDU]（无 CRC）
 * 线圈位按字节打包（低 bit 在前），寄存器按大端 uint16。
 */
#include "modbus.h"
#include "crc.h"

#include <string.h>

int modbus_init(void)
{
    return IOT_OK;
}

uint16_t modbus_crc16(const uint8_t *data, uint32_t len)
{
    return crc16_modbus(data, len);
}

/* ================= 基础编解码 ================= */
static void put_be16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static uint16_t get_be16(const uint8_t *p)
{
    return (uint16_t)((p[0] << 8) | p[1]);
}

static uint32_t bits_to_bytes(uint16_t qty)
{
    return (uint32_t)(qty + 7) / 8;
}

/* ================= RTU / TCP 成帧 ================= */
/* 把 PDU（pdu/pdu_len）封装为目标模式 ADU；返回 ADU 长度，0 表示容量不足 */
static uint32_t frame_adu(modbus_mode_t mode, uint8_t slave,
                          const uint8_t *pdu, uint32_t pdu_len,
                          uint8_t *out, uint32_t capacity)
{
    if (mode == MODBUS_TCP) {
        uint32_t total = MODBUS_TCP_MBAP_LEN + pdu_len;
        if (total > capacity) {
            return 0;
        }
        put_be16(&out[0], 0x0001);          /* transaction id */
        put_be16(&out[2], 0x0000);          /* protocol id */
        put_be16(&out[4], (uint16_t)(pdu_len + 1)); /* unit + PDU */
        out[6] = slave;                     /* unit id */
        memcpy(&out[7], pdu, pdu_len);
        return total;
    }

    {
        uint32_t total = 1 + pdu_len + 2;
        uint16_t crc;
        if (total > capacity) {
            return 0;
        }
        out[0] = slave;
        memcpy(&out[1], pdu, pdu_len);
        crc = modbus_crc16(out, 1 + pdu_len);
        out[1 + pdu_len] = (uint8_t)(crc & 0xFF);
        out[2 + pdu_len] = (uint8_t)(crc >> 8);
        return total;
    }
}

/* 解帧：校验并抽出 PDU（去掉 slave/MBAP/CRC）。返回负值表示非法帧 */
static int unframe_adu(modbus_mode_t mode, uint8_t *slave,
                       const uint8_t *adu, uint32_t len,
                       const uint8_t **pdu, uint32_t *pdu_len)
{
    if (mode == MODBUS_TCP) {
        uint16_t declared;
        if (adu == NULL || len < MODBUS_TCP_MBAP_LEN + 2) {
            return -1;
        }
        if (get_be16(&adu[2]) != 0x0000) {   /* protocol id 必须为 0 */
            return -1;
        }
        declared = get_be16(&adu[4]);
        if ((uint32_t)declared + MODBUS_TCP_MBAP_LEN - 1 != len) {
            return -1;               /* 长度不一致 */
        }
        *slave = adu[6];
        *pdu = &adu[7];
        *pdu_len = (uint32_t)declared - 1;
        return IOT_OK;
    }

    {
        uint16_t crc, calc;
        if (adu == NULL || len < 4) {
            return -1;
        }
        crc = (uint16_t)(adu[len - 1] << 8) | adu[len - 2];
        calc = modbus_crc16(adu, len - 2);
        if (crc != calc) {
            return -1;
        }
        *slave = adu[0];
        *pdu = &adu[1];
        *pdu_len = len - 3;
        return IOT_OK;
    }
}

/* ================= 主站 ================= */
int modbus_build_request(modbus_mode_t mode, uint8_t slave, uint8_t fc,
                         uint16_t addr, uint16_t qty,
                         const uint8_t *data, uint16_t data_len,
                         uint8_t *out, uint32_t capacity, uint32_t *out_len)
{
    uint8_t pdu[MODBUS_TCP_MAX_ADU];
    uint32_t pdu_len = 0;
    uint32_t total;

    if (out == NULL || out_len == NULL) {
        return -1;
    }

    switch (fc) {
    case MODBUS_FC_READ_COILS:
    case MODBUS_FC_READ_DISCRETE_INPUTS:
    case MODBUS_FC_READ_HOLDING_REGS:
    case MODBUS_FC_READ_INPUT_REGS:
        if (qty == 0 || qty > (fc <= 2 ? 2000 : 125)) {
            return -1;
        }
        pdu[0] = fc;
        put_be16(&pdu[1], addr);
        put_be16(&pdu[3], qty);
        pdu_len = 5;
        break;

    case MODBUS_FC_WRITE_SINGLE_COIL:
        pdu[0] = fc;
        put_be16(&pdu[1], addr);
        put_be16(&pdu[3], (data_len > 0 && data[0]) ? 0xFF00 : 0x0000);
        pdu_len = 5;
        break;

    case MODBUS_FC_WRITE_SINGLE_REG:
        pdu[0] = fc;
        put_be16(&pdu[1], addr);
        if (data_len >= 2) {
            pdu[3] = data[0];
            pdu[4] = data[1];
        } else {
            pdu[3] = 0;
            pdu[4] = 0;
        }
        pdu_len = 5;
        break;

    case MODBUS_FC_WRITE_MULTI_COILS:
        if (qty == 0 || qty > 1968 || data == NULL ||
            data_len != bits_to_bytes(qty)) {
            return -1;
        }
        pdu[0] = fc;
        put_be16(&pdu[1], addr);
        put_be16(&pdu[3], qty);
        pdu[5] = (uint8_t)data_len;
        memcpy(&pdu[6], data, data_len);
        pdu_len = 6 + data_len;
        break;

    case MODBUS_FC_WRITE_MULTI_REGS:
        if (qty == 0 || qty > 123 || data == NULL ||
            data_len != qty * 2) {
            return -1;
        }
        pdu[0] = fc;
        put_be16(&pdu[1], addr);
        put_be16(&pdu[3], qty);
        pdu[5] = (uint8_t)data_len;
        memcpy(&pdu[6], data, data_len);
        pdu_len = 6 + data_len;
        break;

    default:
        return -1;
    }

    total = frame_adu(mode, slave, pdu, pdu_len, out, capacity);
    if (total == 0) {
        return -1;
    }
    *out_len = total;
    return IOT_OK;
}

int modbus_parse_response(modbus_mode_t mode, const uint8_t *adu, uint32_t len,
                          uint8_t *fc, uint8_t *out_data, uint32_t cap,
                          uint32_t *out_data_len)
{
    uint8_t slave;
    const uint8_t *pdu;
    uint32_t pdu_len;
    int r;

    r = unframe_adu(mode, &slave, adu, len, &pdu, &pdu_len);
    if (r != IOT_OK || pdu_len < 2) {
        return -1;
    }

    if (fc != NULL) {
        *fc = pdu[0] & 0x7F;
    }

    /* 异常响应：fc 最高位置位 */
    if (pdu[0] & 0x80) {
        return (int)(pdu_len >= 2) ? pdu[1] : MODBUS_EX_DEVICE_FAILURE;
    }

    if (out_data != NULL && out_data_len != NULL) {
        uint32_t n = pdu_len - 1;
        if (n > cap) {
            n = cap;
        }
        memcpy(out_data, &pdu[1], n);
        *out_data_len = n;
    }
    return IOT_OK;
}

/* ================= 从站 ================= */
static uint32_t slave_exception(uint8_t fc, uint8_t code, uint8_t *pdu)
{
    pdu[0] = (uint8_t)(fc | 0x80);
    pdu[1] = code;
    return 2;
}

/* 处理 PDU，返回响应 PDU 长度，0 表示不回复 */
static uint32_t slave_pdu(const modbus_slave_if_t *iface,
                          const uint8_t *pdu, uint32_t pdu_len,
                          uint8_t *out, uint32_t capacity)
{
    uint8_t fc;
    uint16_t addr;
    (void)capacity;

    if (pdu_len < 2 || iface == NULL) {
        return 0;
    }
    fc = pdu[0];
    addr = get_be16(&pdu[1]);

    switch (fc) {
    case MODBUS_FC_READ_COILS:
    case MODBUS_FC_READ_DISCRETE_INPUTS:
    case MODBUS_FC_READ_HOLDING_REGS:
    case MODBUS_FC_READ_INPUT_REGS: {
        uint8_t bits[256];
        uint16_t regs[125];
        uint16_t count;
        modbus_read_bits_fn rb;
        modbus_read_regs_fn rr;
        int ok;

        if (pdu_len < 5) {
            return 0;
        }
        count = get_be16(&pdu[3]);
        if (count == 0) {
            return slave_exception(fc, MODBUS_EX_ILLEGAL_DATA_VALUE, out);
        }

        if (fc == MODBUS_FC_READ_COILS) {
            rb = iface->read_coils;
            if (rb == NULL) {
                return slave_exception(fc, MODBUS_EX_ILLEGAL_FUNCTION, out);
            }
            ok = rb(addr, count, bits, iface->user);
            if (ok != 0) {
                return slave_exception(fc, MODBUS_EX_ILLEGAL_DATA_ADDR, out);
            }
            {
                uint32_t bytes = bits_to_bytes(count);
                uint32_t i;
                out[0] = fc;
                out[1] = (uint8_t)bytes;
                for (i = 0; i < bytes; i++) {
                    out[2 + i] = bits[i];
                }
                return 2 + bytes;
            }
        } else if (fc == MODBUS_FC_READ_DISCRETE_INPUTS) {
            rb = iface->read_discrete;
            if (rb == NULL) {
                return slave_exception(fc, MODBUS_EX_ILLEGAL_FUNCTION, out);
            }
            ok = rb(addr, count, bits, iface->user);
            if (ok != 0) {
                return slave_exception(fc, MODBUS_EX_ILLEGAL_DATA_ADDR, out);
            }
            {
                uint32_t bytes = bits_to_bytes(count);
                uint32_t i;
                out[0] = fc;
                out[1] = (uint8_t)bytes;
                for (i = 0; i < bytes; i++) {
                    out[2 + i] = bits[i];
                }
                return 2 + bytes;
            }
        } else if (fc == MODBUS_FC_READ_HOLDING_REGS) {
            rr = iface->read_holding;
            if (rr == NULL) {
                return slave_exception(fc, MODBUS_EX_ILLEGAL_FUNCTION, out);
            }
            ok = rr(addr, count, regs, iface->user);
            if (ok != 0) {
                return slave_exception(fc, MODBUS_EX_ILLEGAL_DATA_ADDR, out);
            }
            {
                uint32_t bytes = (uint32_t)count * 2;
                uint32_t i;
                out[0] = fc;
                out[1] = (uint8_t)bytes;
                for (i = 0; i < count; i++) {
                    put_be16(&out[2 + i * 2], regs[i]);
                }
                return 2 + bytes;
            }
        } else { /* FC04 */
            rr = iface->read_input;
            if (rr == NULL) {
                return slave_exception(fc, MODBUS_EX_ILLEGAL_FUNCTION, out);
            }
            ok = rr(addr, count, regs, iface->user);
            if (ok != 0) {
                return slave_exception(fc, MODBUS_EX_ILLEGAL_DATA_ADDR, out);
            }
            {
                uint32_t bytes = (uint32_t)count * 2;
                uint32_t i;
                out[0] = fc;
                out[1] = (uint8_t)bytes;
                for (i = 0; i < count; i++) {
                    put_be16(&out[2 + i * 2], regs[i]);
                }
                return 2 + bytes;
            }
        }
    }

    case MODBUS_FC_WRITE_SINGLE_COIL:
    case MODBUS_FC_WRITE_SINGLE_REG: {
        uint8_t bit;
        uint16_t reg;
        int ok;
        if (pdu_len < 5) {
            return 0;
        }
        if (fc == MODBUS_FC_WRITE_SINGLE_COIL) {
            if (iface->write_coils == NULL) {
                return slave_exception(fc, MODBUS_EX_ILLEGAL_FUNCTION, out);
            }
            bit = (get_be16(&pdu[3]) == 0xFF00) ? 1 : 0;
            ok = iface->write_coils(addr, &bit, 1, iface->user);
        } else {
            if (iface->write_regs == NULL) {
                return slave_exception(fc, MODBUS_EX_ILLEGAL_FUNCTION, out);
            }
            reg = get_be16(&pdu[3]);
            ok = iface->write_regs(addr, &reg, 1, iface->user);
        }
        if (ok != 0) {
            return slave_exception(fc, MODBUS_EX_ILLEGAL_DATA_VALUE, out);
        }
        memcpy(out, pdu, pdu_len);   /* 写单点：响应回显请求 */
        return pdu_len;
    }

    case MODBUS_FC_WRITE_MULTI_COILS:
    case MODBUS_FC_WRITE_MULTI_REGS: {
        uint8_t byte_count;
        uint16_t qty2;
        const uint8_t *vals;
        uint16_t n;
        int ok;
        if (pdu_len < 6) {
            return 0;
        }
        qty2 = get_be16(&pdu[3]);
        byte_count = pdu[5];
        vals = &pdu[6];
        n = qty2;

        if (fc == MODBUS_FC_WRITE_MULTI_COILS) {
            if (iface->write_coils == NULL || byte_count != bits_to_bytes(qty2)) {
                return slave_exception(fc, MODBUS_EX_ILLEGAL_FUNCTION, out);
            }
            ok = iface->write_coils(addr, vals, n, iface->user);
        } else {
            uint16_t regs[123];
            uint16_t count = (uint16_t)(byte_count / 2);
            uint32_t i;
            if (iface->write_regs == NULL || count != qty2) {
                return slave_exception(fc, MODBUS_EX_ILLEGAL_FUNCTION, out);
            }
            for (i = 0; i < count; i++) {
                regs[i] = get_be16(&vals[i * 2]);
            }
            ok = iface->write_regs(addr, regs, count, iface->user);
            (void)n;
        }
        if (ok != 0) {
            return slave_exception(fc, MODBUS_EX_ILLEGAL_DATA_VALUE, out);
        }
        out[0] = fc;
        put_be16(&out[1], addr);
        put_be16(&out[3], qty2);
        return 5;
    }

    default:
        return slave_exception(fc, MODBUS_EX_ILLEGAL_FUNCTION, out);
    }
}

int modbus_slave_process(modbus_mode_t mode, uint8_t slave,
                         const modbus_slave_if_t *iface,
                         const uint8_t *adu, uint32_t len,
                         uint8_t *out, uint32_t capacity, uint32_t *out_len)
{
    uint8_t s;
    const uint8_t *pdu;
    uint32_t pdu_len;
    uint8_t resp[MODBUS_TCP_MAX_ADU - MODBUS_TCP_MBAP_LEN];
    uint32_t resp_len;
    uint32_t total;
    int r;

    if (iface == NULL || out == NULL || out_len == NULL) {
        return -1;
    }
    r = unframe_adu(mode, &s, adu, len, &pdu, &pdu_len);
    if (r != IOT_OK) {
        return r;
    }
    if (s != slave) {
        return -1;   /* 不是本机地址，不回复 */
    }

    resp_len = slave_pdu(iface, pdu, pdu_len, resp, sizeof(resp));
    if (resp_len == 0) {
        return -1;
    }

    total = frame_adu(mode, slave, resp, resp_len, out, capacity);
    if (total == 0) {
        return -1;
    }
    *out_len = total;
    return IOT_OK;
}