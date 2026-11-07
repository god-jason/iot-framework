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

/* ================= 主站：传输与寄存器读写 ================= */
int modbus_master_init(modbus_master_t *master, modbus_mode_t mode, uint8_t address,
                       modbus_transport_fn transport, void *user, uint32_t timeout_ms)
{
    if (master == NULL) {
        return MODBUS_ERR_PARAM;
    }
    memset(master, 0, sizeof(*master));
    master->mode = mode;
    master->address = address;
    master->transport = transport;
    master->user = user;
    master->timeout_ms = timeout_ms;
    master->transaction_id = 0;
    return IOT_OK;
}

void modbus_master_set_transport(modbus_master_t *master, modbus_transport_fn transport, void *user)
{
    if (master == NULL) {
        return;
    }
    master->transport = transport;
    master->user = user;
}

void modbus_master_set_address(modbus_master_t *master, uint8_t address)
{
    if (master != NULL) {
        master->address = address;
    }
}

/* 一次完整事务：组帧 -> transport -> 解帧 -> 校验功能码。
 * resp 返回 PDU 数据区（含读响应中的字节数字）。 */
static int master_transact(modbus_master_t *master, uint8_t fc,
                           uint16_t addr, uint16_t qty,
                           const uint8_t *wdata, uint16_t wlen,
                           uint8_t *resp, uint32_t resp_cap, uint32_t *resp_len)
{
    uint8_t req[MODBUS_TCP_MAX_ADU];
    uint8_t rsp[MODBUS_TCP_MAX_ADU];
    uint32_t req_len = 0;
    uint32_t rsp_len = 0;
    uint32_t data_len = 0;
    uint8_t rfc = 0;
    int r;

    if (master == NULL || master->transport == NULL || resp == NULL) {
        return MODBUS_ERR_PARAM;
    }
    if (modbus_build_request(master->mode, master->address, fc, addr, qty,
                             wdata, wlen, req, sizeof(req), &req_len) != IOT_OK) {
        return MODBUS_ERR_PARAM;
    }
    if (master->mode == MODBUS_TCP) {
        master->transaction_id++;
        req[0] = (uint8_t)(master->transaction_id >> 8);
        req[1] = (uint8_t)(master->transaction_id & 0xFF);
    }

    master->request_count++;
    r = master->transport(master->user, req, req_len, rsp, sizeof(rsp),
                          &rsp_len, master->timeout_ms);
    if (r != 0 || rsp_len == 0) {
        master->error_count++;
        return MODBUS_ERR_TRANSPORT;
    }

    r = modbus_parse_response(master->mode, rsp, rsp_len, &rfc,
                              resp, resp_cap, &data_len);
    if (r > 0) {
        master->error_count++;
        return r;                       /* Modbus 异常码 */
    }
    if (r != IOT_OK) {
        master->error_count++;
        return MODBUS_ERR_FRAME;
    }
    if (rfc != fc) {
        master->error_count++;
        return MODBUS_ERR_FRAME;
    }
    if (resp_len != NULL) {
        *resp_len = data_len;
    }
    return IOT_OK;
}

int modbus_master_read_coils(modbus_master_t *master, uint16_t addr, uint16_t qty, uint8_t *bits)
{
    uint8_t buf[MODBUS_TCP_MAX_ADU];
    uint32_t len = 0;
    int r;

    if (bits == NULL || qty == 0 || qty > 2000) {
        return MODBUS_ERR_PARAM;
    }
    r = master_transact(master, MODBUS_FC_READ_COILS, addr, qty, NULL, 0,
                        buf, sizeof(buf), &len);
    if (r != IOT_OK) {
        return r;
    }
    if (len < 1 || buf[0] != (uint8_t)bits_to_bytes(qty) || len < 1u + buf[0]) {
        return MODBUS_ERR_FRAME;
    }
    memcpy(bits, &buf[1], buf[0]);
    return IOT_OK;
}

int modbus_master_read_discrete(modbus_master_t *master, uint16_t addr, uint16_t qty, uint8_t *bits)
{
    uint8_t buf[MODBUS_TCP_MAX_ADU];
    uint32_t len = 0;
    int r;

    if (bits == NULL || qty == 0 || qty > 2000) {
        return MODBUS_ERR_PARAM;
    }
    r = master_transact(master, MODBUS_FC_READ_DISCRETE_INPUTS, addr, qty, NULL, 0,
                        buf, sizeof(buf), &len);
    if (r != IOT_OK) {
        return r;
    }
    if (len < 1 || buf[0] != (uint8_t)bits_to_bytes(qty) || len < 1u + buf[0]) {
        return MODBUS_ERR_FRAME;
    }
    memcpy(bits, &buf[1], buf[0]);
    return IOT_OK;
}

static int master_read_regs(modbus_master_t *master, uint8_t fc,
                            uint16_t addr, uint16_t qty, uint16_t *regs)
{
    uint8_t buf[MODBUS_TCP_MAX_ADU];
    uint32_t len = 0;
    uint16_t i;
    int r;

    if (regs == NULL || qty == 0 || qty > 125) {
        return MODBUS_ERR_PARAM;
    }
    r = master_transact(master, fc, addr, qty, NULL, 0, buf, sizeof(buf), &len);
    if (r != IOT_OK) {
        return r;
    }
    if (len < 1 || buf[0] != (uint8_t)(qty * 2) || len < 1u + (uint32_t)qty * 2u) {
        return MODBUS_ERR_FRAME;
    }
    for (i = 0; i < qty; i++) {
        regs[i] = get_be16(&buf[1 + (uint32_t)i * 2]);
    }
    return IOT_OK;
}

int modbus_master_read_holding(modbus_master_t *master, uint16_t addr, uint16_t qty, uint16_t *regs)
{
    return master_read_regs(master, MODBUS_FC_READ_HOLDING_REGS, addr, qty, regs);
}

int modbus_master_read_input(modbus_master_t *master, uint16_t addr, uint16_t qty, uint16_t *regs)
{
    return master_read_regs(master, MODBUS_FC_READ_INPUT_REGS, addr, qty, regs);
}

int modbus_master_write_coil(modbus_master_t *master, uint16_t addr, uint8_t on)
{
    uint8_t wdata[1];
    uint8_t buf[MODBUS_TCP_MAX_ADU];
    uint32_t len = 0;
    int r;

    wdata[0] = on ? 1U : 0U;
    r = master_transact(master, MODBUS_FC_WRITE_SINGLE_COIL, addr, 1, wdata, 1,
                        buf, sizeof(buf), &len);
    if (r != IOT_OK) {
        return r;
    }
    if (len != 4 || get_be16(buf) != addr ||
        get_be16(&buf[2]) != (on ? 0xFF00u : 0x0000u)) {
        return MODBUS_ERR_FRAME;
    }
    return IOT_OK;
}

int modbus_master_write_reg(modbus_master_t *master, uint16_t addr, uint16_t value)
{
    uint8_t wdata[2];
    uint8_t buf[MODBUS_TCP_MAX_ADU];
    uint32_t len = 0;
    int r;

    wdata[0] = (uint8_t)(value >> 8);
    wdata[1] = (uint8_t)(value & 0xFF);
    r = master_transact(master, MODBUS_FC_WRITE_SINGLE_REG, addr, 1, wdata, 2,
                        buf, sizeof(buf), &len);
    if (r != IOT_OK) {
        return r;
    }
    if (len != 4 || get_be16(buf) != addr || get_be16(&buf[2]) != value) {
        return MODBUS_ERR_FRAME;
    }
    return IOT_OK;
}

int modbus_master_write_coils(modbus_master_t *master, uint16_t addr, uint16_t qty,
                              const uint8_t *bits)
{
    uint8_t buf[MODBUS_TCP_MAX_ADU];
    uint32_t len = 0;
    uint16_t bytes;
    int r;

    if (bits == NULL || qty == 0 || qty > 1968) {
        return MODBUS_ERR_PARAM;
    }
    bytes = (uint16_t)bits_to_bytes(qty);
    r = master_transact(master, MODBUS_FC_WRITE_MULTI_COILS, addr, qty,
                        bits, bytes, buf, sizeof(buf), &len);
    if (r != IOT_OK) {
        return r;
    }
    if (len != 4 || get_be16(buf) != addr || get_be16(&buf[2]) != qty) {
        return MODBUS_ERR_FRAME;
    }
    return IOT_OK;
}

int modbus_master_write_regs(modbus_master_t *master, uint16_t addr, uint16_t qty,
                             const uint16_t *regs)
{
    uint8_t wdata[246];
    uint8_t buf[MODBUS_TCP_MAX_ADU];
    uint32_t len = 0;
    uint16_t i;
    int r;

    if (regs == NULL || qty == 0 || qty > 123) {
        return MODBUS_ERR_PARAM;
    }
    for (i = 0; i < qty; i++) {
        put_be16(&wdata[(uint32_t)i * 2], regs[i]);
    }
    r = master_transact(master, MODBUS_FC_WRITE_MULTI_REGS, addr, qty,
                        wdata, (uint16_t)(qty * 2), buf, sizeof(buf), &len);
    if (r != IOT_OK) {
        return r;
    }
    if (len != 4 || get_be16(buf) != addr || get_be16(&buf[2]) != qty) {
        return MODBUS_ERR_FRAME;
    }
    return IOT_OK;
}

/* ================= 从站：寄存器映射表 ================= */
#define MODBUS_MAP_CHUNK 32

void modbus_slave_register_init(modbus_slave_t *slave, uint8_t address, modbus_mode_t mode)
{
    if (slave == NULL) {
        return;
    }
    memset(slave, 0, sizeof(*slave));
    slave->address = address;
    slave->mode = mode;
}

int modbus_slave_register_range(modbus_slave_t *slave, const modbus_reg_range_t *range)
{
    modbus_reg_range_t *slot;

    if (slave == NULL || range == NULL || range->qty == 0 ||
        (uint32_t)range->addr + range->qty > 0x10000u) {
        return MODBUS_ERR_PARAM;
    }
    if (slave->range_count >= MODBUS_SLAVE_MAX_RANGES) {
        return MODBUS_ERR_NO_MEMORY;
    }
    slot = &slave->ranges[slave->range_count++];
    *slot = *range;
    return IOT_OK;
}

int modbus_slave_map_regs(modbus_slave_t *slave, uint16_t addr,
                          uint16_t *data, uint16_t qty, modbus_reg_type_t type)
{
    modbus_reg_range_t range;

    if (data == NULL) {
        return MODBUS_ERR_PARAM;
    }
    memset(&range, 0, sizeof(range));
    range.type = type;
    range.addr = addr;
    range.qty = qty;
    range.data = data;
    return modbus_slave_register_range(slave, &range);
}

int modbus_slave_map_bits(modbus_slave_t *slave, uint16_t addr,
                          uint8_t *bits, uint16_t qty, modbus_reg_type_t type)
{
    modbus_reg_range_t range;

    if (bits == NULL) {
        return MODBUS_ERR_PARAM;
    }
    memset(&range, 0, sizeof(range));
    range.type = type;
    range.addr = addr;
    range.qty = qty;
    range.bits = bits;
    return modbus_slave_register_range(slave, &range);
}

int modbus_slave_set_callback(modbus_slave_t *slave, modbus_reg_type_t type, uint16_t addr,
                              modbus_slave_read_cb read_cb, modbus_slave_write_cb write_cb,
                              void *user)
{
    uint16_t i;

    if (slave == NULL) {
        return MODBUS_ERR_PARAM;
    }
    for (i = 0; i < slave->range_count; i++) {
        if (slave->ranges[i].type == type && slave->ranges[i].addr == addr) {
            slave->ranges[i].read_cb = read_cb;
            slave->ranges[i].write_cb = write_cb;
            slave->ranges[i].user = user;
            return IOT_OK;
        }
    }
    return MODBUS_ERR_RANGE;
}

int modbus_slave_unregister_range(modbus_slave_t *slave, modbus_reg_type_t type, uint16_t addr)
{
    uint16_t i;

    if (slave == NULL) {
        return MODBUS_ERR_PARAM;
    }
    for (i = 0; i < slave->range_count; i++) {
        if (slave->ranges[i].type == type && slave->ranges[i].addr == addr) {
            if (i + 1 < slave->range_count) {
                memmove(&slave->ranges[i], &slave->ranges[i + 1],
                        (slave->range_count - i - 1) * sizeof(slave->ranges[0]));
            }
            slave->range_count--;
            return IOT_OK;
        }
    }
    return MODBUS_ERR_RANGE;
}

static modbus_reg_range_t *map_find_range(modbus_slave_t *slave, modbus_reg_type_t type,
                                          uint16_t addr, uint16_t qty)
{
    uint16_t i;

    if (qty == 0) {
        return NULL;
    }
    for (i = 0; i < slave->range_count; i++) {
        modbus_reg_range_t *r = &slave->ranges[i];
        if (r->type != type) {
            continue;
        }
        if (addr < r->addr) {
            continue;
        }
        if ((uint32_t)addr + qty > (uint32_t)r->addr + r->qty) {
            continue;
        }
        return r;
    }
    return NULL;
}

static int map_read_values(modbus_reg_range_t *r, uint16_t addr, uint16_t qty, uint16_t *values)
{
    uint16_t i;

    if (r->read_cb != NULL) {
        return r->read_cb(addr, values, qty, r->user);
    }
    if (r->data != NULL) {
        for (i = 0; i < qty; i++) {
            values[i] = r->data[(uint32_t)(addr - r->addr) + i];
        }
        return IOT_OK;
    }
    if (r->bits != NULL) {
        for (i = 0; i < qty; i++) {
            values[i] = r->bits[(uint32_t)(addr - r->addr) + i] ? 1U : 0U;
        }
        return IOT_OK;
    }
    return -1;
}

static int map_write_values(modbus_reg_range_t *r, uint16_t addr, uint16_t qty,
                            const uint16_t *values)
{
    uint16_t i;

    if (r->write_cb != NULL) {
        return r->write_cb(addr, values, qty, r->user);
    }
    if (r->data != NULL) {
        for (i = 0; i < qty; i++) {
            r->data[(uint32_t)(addr - r->addr) + i] = values[i];
        }
        return IOT_OK;
    }
    if (r->bits != NULL) {
        for (i = 0; i < qty; i++) {
            r->bits[(uint32_t)(addr - r->addr) + i] = values[i] ? 1U : 0U;
        }
        return IOT_OK;
    }
    return -1;
}

static uint32_t map_exception(modbus_slave_t *slave, uint8_t fc, uint8_t code, uint8_t *out)
{
    slave->exception_count++;
    return slave_exception(fc, code, out);
}

/* 处理寄存器映射从站的 PDU */
static uint32_t map_pdu(modbus_slave_t *slave, const uint8_t *pdu, uint32_t pdu_len,
                        uint8_t *out, uint32_t capacity)
{
    uint8_t fc;
    uint16_t addr;
    modbus_reg_range_t *range;

    if (pdu_len < 2) {
        return 0;
    }
    fc = pdu[0];
    addr = get_be16(&pdu[1]);

    switch (fc) {
    case MODBUS_FC_READ_COILS:
    case MODBUS_FC_READ_DISCRETE_INPUTS: {
        uint16_t qty;
        uint16_t done = 0;
        uint32_t bytes;
        modbus_reg_type_t type = (fc == MODBUS_FC_READ_COILS)
                                 ? MODBUS_REG_COIL : MODBUS_REG_DISCRETE;
        if (pdu_len < 5) {
            return 0;
        }
        qty = get_be16(&pdu[3]);
        if (qty == 0 || qty > 2000) {
            return map_exception(slave, fc, MODBUS_EX_ILLEGAL_DATA_VALUE, out);
        }
        range = map_find_range(slave, type, addr, qty);
        if (range == NULL) {
            return map_exception(slave, fc, MODBUS_EX_ILLEGAL_DATA_ADDR, out);
        }
        bytes = bits_to_bytes(qty);
        if (capacity < 2u + bytes) {
            return 0;
        }
        out[0] = fc;
        out[1] = (uint8_t)bytes;
        memset(&out[2], 0, bytes);
        while (done < qty) {
            uint16_t chunk = (uint16_t)((qty - done) > 256 ? 256 : (qty - done));
            uint16_t i;
            uint16_t values[256];
            if (map_read_values(range, (uint16_t)(addr + done), chunk, values) != IOT_OK) {
                return map_exception(slave, fc, MODBUS_EX_DEVICE_FAILURE, out);
            }
            for (i = 0; i < chunk; i++) {
                if (values[i] & 1U) {
                    uint16_t bit = (uint16_t)(done + i);
                    out[2 + (bit >> 3)] |= (uint8_t)(1U << (bit & 7));
                }
            }
            done = (uint16_t)(done + chunk);
        }
        return 2u + bytes;
    }

    case MODBUS_FC_READ_HOLDING_REGS:
    case MODBUS_FC_READ_INPUT_REGS: {
        uint16_t qty;
        uint16_t i;
        uint16_t values[125];
        modbus_reg_type_t type = (fc == MODBUS_FC_READ_HOLDING_REGS)
                                 ? MODBUS_REG_HOLDING : MODBUS_REG_INPUT;
        if (pdu_len < 5) {
            return 0;
        }
        qty = get_be16(&pdu[3]);
        if (qty == 0 || qty > 125) {
            return map_exception(slave, fc, MODBUS_EX_ILLEGAL_DATA_VALUE, out);
        }
        range = map_find_range(slave, type, addr, qty);
        if (range == NULL) {
            return map_exception(slave, fc, MODBUS_EX_ILLEGAL_DATA_ADDR, out);
        }
        if (map_read_values(range, addr, qty, values) != IOT_OK) {
            return map_exception(slave, fc, MODBUS_EX_DEVICE_FAILURE, out);
        }
        if (capacity < 2u + (uint32_t)qty * 2u) {
            return 0;
        }
        out[0] = fc;
        out[1] = (uint8_t)(qty * 2);
        for (i = 0; i < qty; i++) {
            put_be16(&out[2 + (uint32_t)i * 2], values[i]);
        }
        return 2u + (uint32_t)qty * 2u;
    }

    case MODBUS_FC_WRITE_SINGLE_COIL: {
        uint16_t raw;
        uint16_t value;
        if (pdu_len < 5) {
            return 0;
        }
        raw = get_be16(&pdu[3]);
        if (raw != 0xFF00 && raw != 0x0000) {
            return map_exception(slave, fc, MODBUS_EX_ILLEGAL_DATA_VALUE, out);
        }
        range = map_find_range(slave, MODBUS_REG_COIL, addr, 1);
        if (range == NULL) {
            return map_exception(slave, fc, MODBUS_EX_ILLEGAL_DATA_ADDR, out);
        }
        value = (raw == 0xFF00) ? 1U : 0U;
        if (map_write_values(range, addr, 1, &value) != IOT_OK) {
            return map_exception(slave, fc, MODBUS_EX_DEVICE_FAILURE, out);
        }
        memcpy(out, pdu, 5);
        return 5;
    }

    case MODBUS_FC_WRITE_SINGLE_REG: {
        uint16_t value;
        if (pdu_len < 5) {
            return 0;
        }
        value = get_be16(&pdu[3]);
        range = map_find_range(slave, MODBUS_REG_HOLDING, addr, 1);
        if (range == NULL) {
            return map_exception(slave, fc, MODBUS_EX_ILLEGAL_DATA_ADDR, out);
        }
        if (map_write_values(range, addr, 1, &value) != IOT_OK) {
            return map_exception(slave, fc, MODBUS_EX_DEVICE_FAILURE, out);
        }
        memcpy(out, pdu, 5);
        return 5;
    }

    case MODBUS_FC_WRITE_MULTI_COILS: {
        uint16_t qty;
        uint8_t byte_count;
        const uint8_t *vals;
        uint16_t done = 0;
        if (pdu_len < 6) {
            return 0;
        }
        qty = get_be16(&pdu[3]);
        byte_count = pdu[5];
        if (qty == 0 || qty > 1968 || byte_count != bits_to_bytes(qty) ||
            pdu_len < 6u + byte_count) {
            return map_exception(slave, fc, MODBUS_EX_ILLEGAL_DATA_VALUE, out);
        }
        range = map_find_range(slave, MODBUS_REG_COIL, addr, qty);
        if (range == NULL) {
            return map_exception(slave, fc, MODBUS_EX_ILLEGAL_DATA_ADDR, out);
        }
        vals = &pdu[6];
        while (done < qty) {
            uint16_t chunk = (uint16_t)((qty - done) > MODBUS_MAP_CHUNK
                                        ? MODBUS_MAP_CHUNK : (qty - done));
            uint16_t i;
            uint16_t values[MODBUS_MAP_CHUNK];
            for (i = 0; i < chunk; i++) {
                uint16_t bit = (uint16_t)(done + i);
                values[i] = (vals[bit >> 3] >> (bit & 7)) & 1U;
            }
            if (map_write_values(range, (uint16_t)(addr + done), chunk, values) != IOT_OK) {
                return map_exception(slave, fc, MODBUS_EX_DEVICE_FAILURE, out);
            }
            done = (uint16_t)(done + chunk);
        }
        out[0] = fc;
        put_be16(&out[1], addr);
        put_be16(&out[3], qty);
        return 5;
    }

    case MODBUS_FC_WRITE_MULTI_REGS: {
        uint16_t qty;
        uint8_t byte_count;
        const uint8_t *vals;
        uint16_t done = 0;
        if (pdu_len < 6) {
            return 0;
        }
        qty = get_be16(&pdu[3]);
        byte_count = pdu[5];
        if (qty == 0 || qty > 123 || byte_count != (uint8_t)(qty * 2) ||
            pdu_len < 6u + byte_count) {
            return map_exception(slave, fc, MODBUS_EX_ILLEGAL_DATA_VALUE, out);
        }
        range = map_find_range(slave, MODBUS_REG_HOLDING, addr, qty);
        if (range == NULL) {
            return map_exception(slave, fc, MODBUS_EX_ILLEGAL_DATA_ADDR, out);
        }
        vals = &pdu[6];
        while (done < qty) {
            uint16_t chunk = (uint16_t)((qty - done) > MODBUS_MAP_CHUNK
                                        ? MODBUS_MAP_CHUNK : (qty - done));
            uint16_t i;
            uint16_t values[MODBUS_MAP_CHUNK];
            for (i = 0; i < chunk; i++) {
                values[i] = get_be16(&vals[(uint32_t)(done + i) * 2]);
            }
            if (map_write_values(range, (uint16_t)(addr + done), chunk, values) != IOT_OK) {
                return map_exception(slave, fc, MODBUS_EX_DEVICE_FAILURE, out);
            }
            done = (uint16_t)(done + chunk);
        }
        out[0] = fc;
        put_be16(&out[1], addr);
        put_be16(&out[3], qty);
        return 5;
    }

    default:
        return map_exception(slave, fc, MODBUS_EX_ILLEGAL_FUNCTION, out);
    }
}

int modbus_slave_poll(modbus_slave_t *slave, const uint8_t *in, uint32_t in_len,
                      uint8_t *out, uint32_t out_capacity, uint32_t *out_len)
{
    uint8_t addr;
    const uint8_t *pdu;
    uint32_t pdu_len;
    uint8_t resp[MODBUS_TCP_MAX_ADU - MODBUS_TCP_MBAP_LEN];
    uint32_t resp_len;
    uint32_t total;
    int r;

    if (slave == NULL || in == NULL || out == NULL || out_len == NULL) {
        return MODBUS_ERR_PARAM;
    }
    r = unframe_adu(slave->mode, &addr, in, in_len, &pdu, &pdu_len);
    if (r != IOT_OK) {
        return MODBUS_ERR_FRAME;
    }
    if (slave->address != 0 && addr != slave->address) {
        return MODBUS_ERR_RANGE;       /* 非本机地址，不回复 */
    }

    slave->request_count++;
    resp_len = map_pdu(slave, pdu, pdu_len, resp, sizeof(resp));
    if (resp_len == 0) {
        return MODBUS_ERR_FRAME;
    }

    if (slave->address == 0) {         /* 广播：处理但不回复 */
        *out_len = 0;
        return IOT_OK;
    }

    total = frame_adu(slave->mode, slave->address, resp, resp_len, out, out_capacity);
    if (total == 0) {
        return MODBUS_ERR_PARAM;
    }
    *out_len = total;
    return IOT_OK;
}
