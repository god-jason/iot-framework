/* src/modbus/modbus.c per-file 单元测试。
 *
 * 覆盖：CRC16、RTU/TCP 组帧字节级断言、容量与数量边界、
 *       各功能码请求构造、响应解析（含异常/CRC/MBAP 错误）、
 *       从站读写各功能码、异常响应、地址过滤、容量不足、
 *       主站完整事务（transport 回调）、寄存器映射从站（直映射/回调/注销/广播/TCP）。
 * 未覆盖：物理串口/网络收发与 RTU 帧间超时。
 */
#include "test_framework.h"
#include "modbus.h"

#include <string.h>

#define REG_COUNT  32
#define COIL_COUNT 32

static uint16_t s_regs[REG_COUNT];
static uint8_t s_coils[COIL_COUNT];

static int rd_bits(uint16_t addr, uint16_t qty, uint8_t *bits, void *user)
{
    uint32_t i;

    (void)user;
    if ((uint32_t)addr + qty > COIL_COUNT) {
        return -1;
    }
    memset(bits, 0, (qty + 7u) / 8u);
    for (i = 0; i < qty; i++) {
        if (s_coils[addr + i]) {
            bits[i / 8u] |= (uint8_t)(1u << (i % 8u));
        }
    }
    return 0;
}

static int rd_regs(uint16_t addr, uint16_t qty, uint16_t *regs, void *user)
{
    uint32_t i;

    (void)user;
    if ((uint32_t)addr + qty > REG_COUNT) {
        return -1;
    }
    for (i = 0; i < qty; i++) {
        regs[i] = s_regs[addr + i];
    }
    return 0;
}

static int wr_bits(uint16_t addr, const uint8_t *bits, uint16_t qty, void *user)
{
    uint32_t i;

    (void)user;
    if ((uint32_t)addr + qty > COIL_COUNT) {
        return -1;
    }
    for (i = 0; i < qty; i++) {
        s_coils[addr + i] = (uint8_t)((bits[i / 8u] >> (i % 8u)) & 1u);
    }
    return 0;
}

static int wr_regs(uint16_t addr, const uint16_t *regs, uint16_t qty, void *user)
{
    uint32_t i;

    (void)user;
    if ((uint32_t)addr + qty > REG_COUNT) {
        return -1;
    }
    for (i = 0; i < qty; i++) {
        s_regs[addr + i] = regs[i];
    }
    return 0;
}

static modbus_slave_if_t make_iface(void)
{
    modbus_slave_if_t iface;

    iface.read_coils = rd_bits;
    iface.read_discrete = rd_bits;
    iface.read_holding = rd_regs;
    iface.read_input = rd_regs;
    iface.write_coils = wr_bits;
    iface.write_regs = wr_regs;
    iface.user = NULL;
    return iface;
}

static uint32_t frame_rtu(uint8_t *out, uint8_t slave,
                          const uint8_t *pdu, uint32_t n)
{
    uint16_t crc;

    out[0] = slave;
    memcpy(&out[1], pdu, n);
    crc = modbus_crc16(out, 1 + n);
    out[1 + n] = (uint8_t)(crc & 0xFF);
    out[2 + n] = (uint8_t)(crc >> 8);
    return n + 3;
}

static uint32_t frame_tcp(uint8_t *out, uint8_t unit,
                          const uint8_t *pdu, uint32_t n)
{
    uint16_t len = (uint16_t)(n + 1);

    out[0] = 0x00;
    out[1] = 0x01;
    out[2] = 0x00;
    out[3] = 0x00;
    out[4] = (uint8_t)(len >> 8);
    out[5] = (uint8_t)(len & 0xFF);
    out[6] = unit;
    memcpy(&out[7], pdu, n);
    return 7 + n;
}

static void test_crc_and_build(void)
{
    uint8_t adu[260];
    uint32_t len = 0;
    static uint8_t big[256];

    tf_begin("modbus/CRC16");
    TF_CHECK_INT(modbus_crc16((const uint8_t *)"123456789", 9), 0x4B37);
    TF_CHECK_INT(modbus_crc16((const uint8_t *)"", 0), 0xFFFF);
    tf_end();

    tf_begin("modbus/RTU 读保持寄存器组帧");
    TF_CHECK_INT(modbus_build_request(MODBUS_RTU, 1, MODBUS_FC_READ_HOLDING_REGS,
                                      0x0010, 2, NULL, 0, adu, sizeof(adu), &len),
                 IOT_OK);
    TF_CHECK_INT(len, 8);
    TF_CHECK_INT(adu[0], 0x01);
    TF_CHECK_INT(adu[1], 0x03);
    TF_CHECK_INT(adu[2], 0x00);
    TF_CHECK_INT(adu[3], 0x10);
    TF_CHECK_INT(adu[4], 0x00);
    TF_CHECK_INT(adu[5], 0x02);
    {
        uint16_t crc = modbus_crc16(adu, 6);
        TF_CHECK_INT(adu[6], crc & 0xFF);
        TF_CHECK_INT(adu[7], (crc >> 8) & 0xFF);
    }
    tf_end();

    tf_begin("modbus/TCP 读保持寄存器组帧");
    TF_CHECK_INT(modbus_build_request(MODBUS_TCP, 0x11, MODBUS_FC_READ_HOLDING_REGS,
                                      0x0001, 1, NULL, 0, adu, sizeof(adu), &len),
                 IOT_OK);
    TF_CHECK_INT(len, 12);
    TF_CHECK_INT(adu[0], 0x00);
    TF_CHECK_INT(adu[1], 0x01);
    TF_CHECK_INT(adu[2], 0x00);
    TF_CHECK_INT(adu[3], 0x00);
    TF_CHECK_INT(adu[4], 0x00);
    TF_CHECK_INT(adu[5], 0x06);
    TF_CHECK_INT(adu[6], 0x11);
    TF_CHECK_INT(adu[7], 0x03);
    TF_CHECK_INT(adu[8], 0x00);
    TF_CHECK_INT(adu[9], 0x01);
    TF_CHECK_INT(adu[10], 0x00);
    TF_CHECK_INT(adu[11], 0x01);
    tf_end();

    tf_begin("modbus/组帧容量边界");
    TF_CHECK_INT(modbus_build_request(MODBUS_RTU, 1, MODBUS_FC_READ_COILS,
                                      0, 1, NULL, 0, adu, 8, &len), IOT_OK);
    TF_CHECK_INT(len, 8);
    TF_CHECK_INT(modbus_build_request(MODBUS_RTU, 1, MODBUS_FC_READ_COILS,
                                      0, 1, NULL, 0, adu, 7, &len), -1);
    TF_CHECK_INT(modbus_build_request(MODBUS_TCP, 1, MODBUS_FC_READ_COILS,
                                      0, 1, NULL, 0, adu, 12, &len), IOT_OK);
    TF_CHECK_INT(modbus_build_request(MODBUS_TCP, 1, MODBUS_FC_READ_COILS,
                                      0, 1, NULL, 0, adu, 11, &len), -1);
    tf_end();

    tf_begin("modbus/读请求数量边界");
    TF_CHECK_INT(modbus_build_request(MODBUS_RTU, 1, MODBUS_FC_READ_COILS,
                                      0, 2000, NULL, 0, adu, sizeof(adu), &len),
                 IOT_OK);
    TF_CHECK_INT(modbus_build_request(MODBUS_RTU, 1, MODBUS_FC_READ_COILS,
                                      0, 2001, NULL, 0, adu, sizeof(adu), &len),
                 -1);
    TF_CHECK_INT(modbus_build_request(MODBUS_RTU, 1, MODBUS_FC_READ_HOLDING_REGS,
                                      0, 125, NULL, 0, adu, sizeof(adu), &len),
                 IOT_OK);
    TF_CHECK_INT(modbus_build_request(MODBUS_RTU, 1, MODBUS_FC_READ_HOLDING_REGS,
                                      0, 126, NULL, 0, adu, sizeof(adu), &len),
                 -1);
    TF_CHECK_INT(modbus_build_request(MODBUS_RTU, 1, MODBUS_FC_READ_DISCRETE_INPUTS,
                                      0, 0, NULL, 0, adu, sizeof(adu), &len),
                 -1);
    tf_end();

    tf_begin("modbus/单点写请求");
    TF_CHECK_INT(modbus_build_request(MODBUS_RTU, 1, MODBUS_FC_WRITE_SINGLE_COIL,
                                      5, 1, big, 0, adu, sizeof(adu), &len),
                 IOT_OK);
    TF_CHECK_INT(adu[3], 0x05);
    TF_CHECK_INT(adu[4], 0x00);
    TF_CHECK_INT(adu[5], 0x00);
    big[0] = 1;
    TF_CHECK_INT(modbus_build_request(MODBUS_RTU, 1, MODBUS_FC_WRITE_SINGLE_COIL,
                                      5, 1, big, 1, adu, sizeof(adu), &len),
                 IOT_OK);
    TF_CHECK_INT(adu[3], 0x05);
    TF_CHECK_INT(adu[4], 0xFF);
    TF_CHECK_INT(adu[5], 0x00);

    big[0] = 0x12;
    big[1] = 0x34;
    TF_CHECK_INT(modbus_build_request(MODBUS_RTU, 1, MODBUS_FC_WRITE_SINGLE_REG,
                                      2, 1, big, 2, adu, sizeof(adu), &len),
                 IOT_OK);
    TF_CHECK_INT(adu[2], 0x00);
    TF_CHECK_INT(adu[3], 0x02);
    TF_CHECK_INT(adu[4], 0x12);
    TF_CHECK_INT(adu[5], 0x34);
    TF_CHECK_INT(modbus_build_request(MODBUS_RTU, 1, MODBUS_FC_WRITE_SINGLE_REG,
                                      2, 1, big, 1, adu, sizeof(adu), &len),
                 IOT_OK);
    TF_CHECK_INT(adu[4], 0x00);
    TF_CHECK_INT(adu[5], 0x00);
    tf_end();

    tf_begin("modbus/多点写请求与数量边界");
    memset(big, 0xCD, sizeof(big));
    TF_CHECK_INT(modbus_build_request(MODBUS_RTU, 1, MODBUS_FC_WRITE_MULTI_COILS,
                                      3, 10, big, 2, adu, sizeof(adu), &len),
                 IOT_OK);
    TF_CHECK_INT(len, 11);
    TF_CHECK_INT(adu[1], MODBUS_FC_WRITE_MULTI_COILS);
    TF_CHECK_INT(adu[4], 0x00);
    TF_CHECK_INT(adu[5], 0x0A);
    TF_CHECK_INT(adu[6], 2);
    TF_CHECK_INT(adu[7], 0xCD);
    TF_CHECK_INT(modbus_build_request(MODBUS_RTU, 1, MODBUS_FC_WRITE_MULTI_COILS,
                                      3, 10, big, 1, adu, sizeof(adu), &len),
                 -1);
    TF_CHECK_INT(modbus_build_request(MODBUS_RTU, 1, MODBUS_FC_WRITE_MULTI_COILS,
                                      3, 1968, big, 246, adu, sizeof(adu), &len),
                 IOT_OK);
    TF_CHECK_INT(modbus_build_request(MODBUS_RTU, 1, MODBUS_FC_WRITE_MULTI_COILS,
                                      3, 1969, big, 247, adu, sizeof(adu), &len),
                 -1);

    TF_CHECK_INT(modbus_build_request(MODBUS_RTU, 1, MODBUS_FC_WRITE_MULTI_REGS,
                                      0, 2, big, 4, adu, sizeof(adu), &len),
                 IOT_OK);
    TF_CHECK_INT(adu[6], 4);
    TF_CHECK_INT(modbus_build_request(MODBUS_RTU, 1, MODBUS_FC_WRITE_MULTI_REGS,
                                      0, 2, big, 3, adu, sizeof(adu), &len),
                 -1);
    TF_CHECK_INT(modbus_build_request(MODBUS_RTU, 1, MODBUS_FC_WRITE_MULTI_REGS,
                                      0, 124, big, 248, adu, sizeof(adu), &len),
                 -1);
    tf_end();

    tf_begin("modbus/请求参数非法");
    TF_CHECK_INT(modbus_build_request(MODBUS_RTU, 1, 0x63, 0, 1, NULL, 0,
                                      adu, sizeof(adu), &len), -1);
    TF_CHECK_INT(modbus_build_request(MODBUS_RTU, 1, MODBUS_FC_READ_COILS,
                                      0, 1, NULL, 0, NULL, 16, &len), -1);
    TF_CHECK_INT(modbus_build_request(MODBUS_RTU, 1, MODBUS_FC_READ_COILS,
                                      0, 1, NULL, 0, adu, sizeof(adu), NULL), -1);
    TF_CHECK_INT(modbus_build_request(MODBUS_RTU, 1, MODBUS_FC_WRITE_MULTI_REGS,
                                      0, 1, NULL, 2, adu, sizeof(adu), &len), -1);
    tf_end();
}

static void test_parse(void)
{
    uint8_t adu[64];
    uint8_t pdu[16];
    uint32_t len;
    uint32_t dlen = 0;
    uint8_t fc = 0;
    uint8_t data[16];

    tf_begin("modbus/解析 RTU 正常响应");
    pdu[0] = MODBUS_FC_READ_HOLDING_REGS;
    pdu[1] = 4;
    pdu[2] = 0x12;
    pdu[3] = 0x34;
    pdu[4] = 0xAB;
    pdu[5] = 0xCD;
    len = frame_rtu(adu, 1, pdu, 6);
    TF_CHECK_INT(modbus_parse_response(MODBUS_RTU, adu, len, &fc,
                                       data, sizeof(data), &dlen), IOT_OK);
    TF_CHECK_INT(fc, 0x03);
    TF_CHECK_INT(dlen, 5);
    TF_CHECK_INT(data[0], 4);
    TF_CHECK_INT(data[1], 0x12);
    TF_CHECK_INT(data[4], 0xCD);
    tf_end();

    tf_begin("modbus/解析缓冲区截断与空输出");
    len = frame_rtu(adu, 1, pdu, 6);
    TF_CHECK_INT(modbus_parse_response(MODBUS_RTU, adu, len, NULL,
                                       data, 2, &dlen), IOT_OK);
    TF_CHECK_INT(dlen, 2);
    TF_CHECK_INT(modbus_parse_response(MODBUS_RTU, adu, len, &fc,
                                       NULL, 0, NULL), IOT_OK);
    tf_end();

    tf_begin("modbus/解析 RTU 异常响应");
    pdu[0] = (uint8_t)(MODBUS_FC_READ_HOLDING_REGS | 0x80);
    pdu[1] = MODBUS_EX_ILLEGAL_DATA_ADDR;
    len = frame_rtu(adu, 1, pdu, 2);
    dlen = 99;
    TF_CHECK_INT(modbus_parse_response(MODBUS_RTU, adu, len, &fc,
                                       data, sizeof(data), &dlen),
                 MODBUS_EX_ILLEGAL_DATA_ADDR);
    TF_CHECK_INT(fc, 0x03);
    TF_CHECK_INT(dlen, 99);        /* 异常响应不写数据 */
    tf_end();

    tf_begin("modbus/解析 RTU 错误帧");
    len = frame_rtu(adu, 1, pdu, 2);
    adu[1] ^= 0xFF;
    TF_CHECK_INT(modbus_parse_response(MODBUS_RTU, adu, len, &fc,
                                       data, sizeof(data), &dlen), -1);
    TF_CHECK_INT(modbus_parse_response(MODBUS_RTU, adu, 3, &fc,
                                       data, sizeof(data), &dlen), -1);
    {
        static const uint8_t one_byte_pdu[] = { 0x01, 0x03, 0x00, 0x00 };
        len = frame_rtu(adu, 1, one_byte_pdu, 1);
        TF_CHECK_INT(modbus_parse_response(MODBUS_RTU, adu, len, &fc,
                                           data, sizeof(data), &dlen), -1);
    }
    tf_end();

    tf_begin("modbus/解析 TCP 正常与异常响应");
    pdu[0] = MODBUS_FC_READ_INPUT_REGS;
    pdu[1] = 2;
    pdu[2] = 0x00;
    pdu[3] = 0x7B;
    len = frame_tcp(adu, 1, pdu, 4);
    TF_CHECK_INT(modbus_parse_response(MODBUS_TCP, adu, len, &fc,
                                       data, sizeof(data), &dlen), IOT_OK);
    TF_CHECK_INT(fc, 0x04);
    TF_CHECK_INT(dlen, 3);
    TF_CHECK_INT(data[2], 0x7B);

    pdu[0] = (uint8_t)(MODBUS_FC_READ_INPUT_REGS | 0x80);
    pdu[1] = MODBUS_EX_DEVICE_FAILURE;
    len = frame_tcp(adu, 1, pdu, 2);
    TF_CHECK_INT(modbus_parse_response(MODBUS_TCP, adu, len, &fc,
                                       data, sizeof(data), &dlen),
                 MODBUS_EX_DEVICE_FAILURE);
    tf_end();

    tf_begin("modbus/解析 TCP 错误帧");
    pdu[0] = MODBUS_FC_READ_HOLDING_REGS;
    pdu[1] = 2;
    pdu[2] = 0x00;
    pdu[3] = 0x01;
    len = frame_tcp(adu, 1, pdu, 4);
    adu[3] = 0x01;                 /* protocol id != 0 */
    TF_CHECK_INT(modbus_parse_response(MODBUS_TCP, adu, len, &fc,
                                       data, sizeof(data), &dlen), -1);
    len = frame_tcp(adu, 1, pdu, 4);
    TF_CHECK_INT(modbus_parse_response(MODBUS_TCP, adu, len - 1, &fc,
                                       data, sizeof(data), &dlen), -1);
    TF_CHECK_INT(modbus_parse_response(MODBUS_TCP, adu, 8, &fc,
                                       data, sizeof(data), &dlen), -1);
    tf_end();
}

static void test_slave_read(void)
{
    modbus_slave_if_t iface = make_iface();
    uint8_t req[64];
    uint8_t resp[64];
    uint8_t data[32];
    uint32_t req_len = 0;
    uint32_t resp_len = 0;
    uint32_t dlen = 0;
    uint8_t fc = 0;

    memset(s_regs, 0, sizeof(s_regs));
    memset(s_coils, 0, sizeof(s_coils));
    s_regs[0] = 0x1234;
    s_regs[1] = 0xABCD;

    tf_begin("modbus/从站 FC03 读保持寄存器");
    TF_CHECK_INT(modbus_build_request(MODBUS_RTU, 1, MODBUS_FC_READ_HOLDING_REGS,
                                      0, 2, NULL, 0, req, sizeof(req), &req_len),
                 IOT_OK);
    TF_CHECK_INT(modbus_slave_process(MODBUS_RTU, 1, &iface, req, req_len,
                                      resp, sizeof(resp), &resp_len), IOT_OK);
    TF_CHECK_INT(resp[0], 0x01);
    TF_CHECK_INT(resp[1], 0x03);
    TF_CHECK_INT(modbus_parse_response(MODBUS_RTU, resp, resp_len, &fc,
                                       data, sizeof(data), &dlen), IOT_OK);
    TF_CHECK_INT(fc, 0x03);
    TF_CHECK_INT(dlen, 5);
    TF_CHECK_INT(data[0], 4);
    TF_CHECK_INT(data[1], 0x12);
    TF_CHECK_INT(data[4], 0xCD);
    tf_end();

    tf_begin("modbus/从站 FC01/FC02 读线圈与离散输入");
    s_coils[0] = 1;
    s_coils[2] = 1;
    TF_CHECK_INT(modbus_build_request(MODBUS_RTU, 1, MODBUS_FC_READ_COILS,
                                      0, 3, NULL, 0, req, sizeof(req), &req_len),
                 IOT_OK);
    TF_CHECK_INT(modbus_slave_process(MODBUS_RTU, 1, &iface, req, req_len,
                                      resp, sizeof(resp), &resp_len), IOT_OK);
    TF_CHECK_INT(modbus_parse_response(MODBUS_RTU, resp, resp_len, &fc,
                                       data, sizeof(data), &dlen), IOT_OK);
    TF_CHECK_INT(dlen, 2);
    TF_CHECK_INT(data[0], 1);
    TF_CHECK_INT(data[1], 0x05);

    TF_CHECK_INT(modbus_build_request(MODBUS_RTU, 1, MODBUS_FC_READ_DISCRETE_INPUTS,
                                      0, 4, NULL, 0, req, sizeof(req), &req_len),
                 IOT_OK);
    TF_CHECK_INT(modbus_slave_process(MODBUS_RTU, 1, &iface, req, req_len,
                                      resp, sizeof(resp), &resp_len), IOT_OK);
    TF_CHECK_INT(modbus_parse_response(MODBUS_RTU, resp, resp_len, &fc,
                                       data, sizeof(data), &dlen), IOT_OK);
    TF_CHECK_INT(fc, 0x02);
    TF_CHECK_INT(dlen, 2);
    TF_CHECK_INT(data[1], 0x05);
    tf_end();

    tf_begin("modbus/从站 FC04 读输入寄存器");
    TF_CHECK_INT(modbus_build_request(MODBUS_RTU, 1, MODBUS_FC_READ_INPUT_REGS,
                                      1, 1, NULL, 0, req, sizeof(req), &req_len),
                 IOT_OK);
    TF_CHECK_INT(modbus_slave_process(MODBUS_RTU, 1, &iface, req, req_len,
                                      resp, sizeof(resp), &resp_len), IOT_OK);
    TF_CHECK_INT(modbus_parse_response(MODBUS_RTU, resp, resp_len, &fc,
                                       data, sizeof(data), &dlen), IOT_OK);
    TF_CHECK_INT(fc, 0x04);
    TF_CHECK_INT(dlen, 3);
    TF_CHECK_INT(data[1], 0xAB);
    TF_CHECK_INT(data[2], 0xCD);
    tf_end();

    tf_begin("modbus/从站 TCP 读保持寄存器");
    TF_CHECK_INT(modbus_build_request(MODBUS_TCP, 1, MODBUS_FC_READ_HOLDING_REGS,
                                      1, 1, NULL, 0, req, sizeof(req), &req_len),
                 IOT_OK);
    TF_CHECK_INT(modbus_slave_process(MODBUS_TCP, 1, &iface, req, req_len,
                                      resp, sizeof(resp), &resp_len), IOT_OK);
    TF_CHECK_INT(modbus_parse_response(MODBUS_TCP, resp, resp_len, &fc,
                                       data, sizeof(data), &dlen), IOT_OK);
    TF_CHECK_INT(data[1], 0xAB);
    TF_CHECK_INT(data[2], 0xCD);
    tf_end();
}

static void test_slave_write(void)
{
    modbus_slave_if_t iface = make_iface();
    uint8_t req[64];
    uint8_t resp[64];
    uint8_t data[32];
    uint32_t req_len = 0;
    uint32_t resp_len = 0;
    uint32_t dlen = 0;
    uint8_t fc = 0;

    memset(s_regs, 0, sizeof(s_regs));
    memset(s_coils, 0, sizeof(s_coils));

    tf_begin("modbus/从站 FC05 写单线圈");
    {
        uint8_t val[1] = { 1 };
        TF_CHECK_INT(modbus_build_request(MODBUS_RTU, 1,
                                          MODBUS_FC_WRITE_SINGLE_COIL, 7, 1,
                                          val, 1, req, sizeof(req), &req_len),
                     IOT_OK);
        TF_CHECK_INT(modbus_slave_process(MODBUS_RTU, 1, &iface, req, req_len,
                                          resp, sizeof(resp), &resp_len),
                     IOT_OK);
        TF_CHECK_INT(s_coils[7], 1);
        TF_CHECK_INT(modbus_parse_response(MODBUS_RTU, resp, resp_len, &fc,
                                           data, sizeof(data), &dlen), IOT_OK);
        TF_CHECK_INT(fc, 0x05);
        TF_CHECK_INT(dlen, 4);
    }
    tf_end();

    tf_begin("modbus/从站 FC06 写单寄存器");
    {
        uint8_t val[2] = { 0x00, 0x2A };
        TF_CHECK_INT(modbus_build_request(MODBUS_RTU, 1,
                                          MODBUS_FC_WRITE_SINGLE_REG, 5, 1,
                                          val, 2, req, sizeof(req), &req_len),
                     IOT_OK);
        TF_CHECK_INT(modbus_slave_process(MODBUS_RTU, 1, &iface, req, req_len,
                                          resp, sizeof(resp), &resp_len),
                     IOT_OK);
        TF_CHECK_INT(s_regs[5], 0x002A);
        tf_end();
    }

    tf_begin("modbus/从站 FC0F 写多线圈");
    {
        uint8_t bits[2] = { 0xCD, 0x01 };
        memset(s_coils, 0, sizeof(s_coils));
        TF_CHECK_INT(modbus_build_request(MODBUS_RTU, 1,
                                          MODBUS_FC_WRITE_MULTI_COILS, 0, 10,
                                          bits, 2, req, sizeof(req), &req_len),
                     IOT_OK);
        TF_CHECK_INT(modbus_slave_process(MODBUS_RTU, 1, &iface, req, req_len,
                                          resp, sizeof(resp), &resp_len),
                     IOT_OK);
        TF_CHECK_INT(s_coils[0], 1);
        TF_CHECK_INT(s_coils[1], 0);
        TF_CHECK_INT(s_coils[2], 1);
        TF_CHECK_INT(s_coils[3], 1);
        TF_CHECK_INT(s_coils[8], 1);
        TF_CHECK_INT(s_coils[9], 0);
        TF_CHECK_INT(modbus_parse_response(MODBUS_RTU, resp, resp_len, &fc,
                                           data, sizeof(data), &dlen), IOT_OK);
        TF_CHECK_INT(fc, 0x0F);
        TF_CHECK_INT(dlen, 4);
        TF_CHECK_INT(data[2], 0x00);
        TF_CHECK_INT(data[3], 0x0A);
    }
    tf_end();

    tf_begin("modbus/从站 FC10 写多寄存器");
    {
        uint8_t vals[4] = { 0x00, 0x11, 0x00, 0x22 };
        TF_CHECK_INT(modbus_build_request(MODBUS_RTU, 1,
                                          MODBUS_FC_WRITE_MULTI_REGS, 0, 2,
                                          vals, 4, req, sizeof(req), &req_len),
                     IOT_OK);
        TF_CHECK_INT(modbus_slave_process(MODBUS_RTU, 1, &iface, req, req_len,
                                          resp, sizeof(resp), &resp_len),
                     IOT_OK);
        TF_CHECK_INT(s_regs[0], 0x0011);
        TF_CHECK_INT(s_regs[1], 0x0022);
    }
    tf_end();
}

static void test_slave_errors(void)
{
    modbus_slave_if_t iface = make_iface();
    modbus_slave_if_t partial = make_iface();
    uint8_t req[64];
    uint8_t resp[64];
    uint8_t pdu[16];
    uint32_t req_len = 0;
    uint32_t resp_len = 0;

    tf_begin("modbus/从站地址过滤与 CRC 错误");
    TF_CHECK_INT(modbus_build_request(MODBUS_RTU, 2, MODBUS_FC_READ_HOLDING_REGS,
                                      0, 1, NULL, 0, req, sizeof(req), &req_len),
                 IOT_OK);
    TF_CHECK_INT(modbus_slave_process(MODBUS_RTU, 1, &iface, req, req_len,
                                      resp, sizeof(resp), &resp_len), -1);

    TF_CHECK_INT(modbus_build_request(MODBUS_RTU, 1, MODBUS_FC_READ_HOLDING_REGS,
                                      0, 1, NULL, 0, req, sizeof(req), &req_len),
                 IOT_OK);
    req[2] ^= 0xFF;
    TF_CHECK_INT(modbus_slave_process(MODBUS_RTU, 1, &iface, req, req_len,
                                      resp, sizeof(resp), &resp_len), -1);
    tf_end();

    tf_begin("modbus/从站参数非法与容量不足");
    TF_CHECK_INT(modbus_build_request(MODBUS_RTU, 1, MODBUS_FC_READ_HOLDING_REGS,
                                      0, 1, NULL, 0, req, sizeof(req), &req_len),
                 IOT_OK);
    TF_CHECK_INT(modbus_slave_process(MODBUS_RTU, 1, NULL, req, req_len,
                                      resp, sizeof(resp), &resp_len), -1);
    TF_CHECK_INT(modbus_slave_process(MODBUS_RTU, 1, &iface, req, req_len,
                                      NULL, sizeof(resp), &resp_len), -1);
    TF_CHECK_INT(modbus_slave_process(MODBUS_RTU, 1, &iface, req, req_len,
                                      resp, sizeof(resp), NULL), -1);
    TF_CHECK_INT(modbus_slave_process(MODBUS_RTU, 1, &iface, req, req_len,
                                      resp, 2, &resp_len), -1);
    tf_end();

    tf_begin("modbus/从站异常响应：非法数据地址");
    TF_CHECK_INT(modbus_build_request(MODBUS_RTU, 1, MODBUS_FC_READ_HOLDING_REGS,
                                      31, 2, NULL, 0, req, sizeof(req), &req_len),
                 IOT_OK);
    TF_CHECK_INT(modbus_slave_process(MODBUS_RTU, 1, &iface, req, req_len,
                                      resp, sizeof(resp), &resp_len), IOT_OK);
    TF_CHECK_INT(resp[1], 0x83);
    TF_CHECK_INT(resp[2], MODBUS_EX_ILLEGAL_DATA_ADDR);
    tf_end();

    tf_begin("modbus/从站异常响应：数量为 0");
    pdu[0] = MODBUS_FC_READ_COILS;
    pdu[1] = 0x00;
    pdu[2] = 0x00;
    pdu[3] = 0x00;
    pdu[4] = 0x00;
    req_len = frame_rtu(req, 1, pdu, 5);
    TF_CHECK_INT(modbus_slave_process(MODBUS_RTU, 1, &iface, req, req_len,
                                      resp, sizeof(resp), &resp_len), IOT_OK);
    TF_CHECK_INT(resp[1], 0x81);
    TF_CHECK_INT(resp[2], MODBUS_EX_ILLEGAL_DATA_VALUE);
    tf_end();

    tf_begin("modbus/从站异常响应：功能未实现");
    partial.read_coils = NULL;
    pdu[0] = MODBUS_FC_READ_COILS;
    pdu[1] = 0x00;
    pdu[2] = 0x00;
    pdu[3] = 0x00;
    pdu[4] = 0x01;
    req_len = frame_rtu(req, 1, pdu, 5);
    TF_CHECK_INT(modbus_slave_process(MODBUS_RTU, 1, &partial, req, req_len,
                                      resp, sizeof(resp), &resp_len), IOT_OK);
    TF_CHECK_INT(resp[1], 0x81);
    TF_CHECK_INT(resp[2], MODBUS_EX_ILLEGAL_FUNCTION);

    partial = make_iface();
    partial.write_coils = NULL;
    pdu[0] = MODBUS_FC_WRITE_SINGLE_COIL;
    pdu[1] = 0x00;
    pdu[2] = 0x01;
    pdu[3] = 0xFF;
    pdu[4] = 0x00;
    req_len = frame_rtu(req, 1, pdu, 5);
    TF_CHECK_INT(modbus_slave_process(MODBUS_RTU, 1, &partial, req, req_len,
                                      resp, sizeof(resp), &resp_len), IOT_OK);
    TF_CHECK_INT(resp[2], MODBUS_EX_ILLEGAL_FUNCTION);
    tf_end();

    tf_begin("modbus/从站异常响应：多点写字节数不匹配");
    partial = make_iface();
    pdu[0] = MODBUS_FC_WRITE_MULTI_COILS;
    pdu[1] = 0x00;
    pdu[2] = 0x00;
    pdu[3] = 0x00;
    pdu[4] = 0x0A;                 /* qty 10 -> 需要 2 字节 */
    pdu[5] = 1;                    /* 实际声明 1 字节 */
    pdu[6] = 0xCD;
    req_len = frame_rtu(req, 1, pdu, 7);
    TF_CHECK_INT(modbus_slave_process(MODBUS_RTU, 1, &partial, req, req_len,
                                      resp, sizeof(resp), &resp_len), IOT_OK);
    TF_CHECK_INT(resp[1], 0x8F);
    TF_CHECK_INT(resp[2], MODBUS_EX_ILLEGAL_FUNCTION);

    pdu[0] = MODBUS_FC_WRITE_MULTI_REGS;
    pdu[5] = 4;                    /* 4 字节 -> 2 个寄存器，与 qty 10 不符 */
    req_len = frame_rtu(req, 1, pdu, 10);
    TF_CHECK_INT(modbus_slave_process(MODBUS_RTU, 1, &partial, req, req_len,
                                      resp, sizeof(resp), &resp_len), IOT_OK);
    TF_CHECK_INT(resp[1], 0x90);
    TF_CHECK_INT(resp[2], MODBUS_EX_ILLEGAL_FUNCTION);
    tf_end();

    tf_begin("modbus/从站未知功能码与过短 PDU");
    pdu[0] = 0x63;
    pdu[1] = 0x00;
    pdu[2] = 0x00;
    req_len = frame_rtu(req, 1, pdu, 2);
    TF_CHECK_INT(modbus_slave_process(MODBUS_RTU, 1, &iface, req, req_len,
                                      resp, sizeof(resp), &resp_len), IOT_OK);
    TF_CHECK_INT(resp[1], 0xE3);
    TF_CHECK_INT(resp[2], MODBUS_EX_ILLEGAL_FUNCTION);

    pdu[0] = MODBUS_FC_READ_COILS;
    req_len = frame_rtu(req, 1, pdu, 1);
    TF_CHECK_INT(modbus_slave_process(MODBUS_RTU, 1, &iface, req, req_len,
                                      resp, sizeof(resp), &resp_len), -1);
    tf_end();

    tf_begin("modbus/从站 TCP 地址过滤");
    TF_CHECK_INT(modbus_build_request(MODBUS_TCP, 7, MODBUS_FC_READ_HOLDING_REGS,
                                      0, 1, NULL, 0, req, sizeof(req), &req_len),
                 IOT_OK);
    TF_CHECK_INT(modbus_slave_process(MODBUS_TCP, 1, &iface, req, req_len,
                                      resp, sizeof(resp), &resp_len), -1);
    tf_end();
}

/* ================= 主站 / 映射从站 联调 ================= */
#define MAP_REG_COUNT 16

static uint16_t s_map_hold[MAP_REG_COUNT];
static uint16_t s_map_input[MAP_REG_COUNT];
static uint8_t s_map_coil[MAP_REG_COUNT];
static uint8_t s_map_disc[MAP_REG_COUNT];

static int s_cb_reads;
static int s_cb_writes;
static uint16_t s_cb_addr;
static uint16_t s_cb_qty;
static uint16_t s_cb_values[32];

static int map_read_cb(uint16_t addr, uint16_t *values, uint16_t qty, void *user)
{
    uint16_t i;

    (void)user;
    s_cb_reads++;
    s_cb_addr = addr;
    s_cb_qty = qty;
    for (i = 0; i < qty; i++) {
        values[i] = (uint16_t)(addr * 10u + i);
    }
    return 0;
}

static int map_write_cb(uint16_t addr, const uint16_t *values, uint16_t qty, void *user)
{
    uint16_t i;

    (void)user;
    s_cb_writes++;
    s_cb_addr = addr;
    s_cb_qty = qty;
    for (i = 0; i < qty && i < 32; i++) {
        s_cb_values[i] = values[i];
    }
    return 0;
}

typedef struct {
    modbus_slave_t *slave;

    uint8_t req[320];
    uint32_t req_len;

    uint8_t resp[320];
    uint32_t resp_len;

    int transport_result;
} mb_loop_t;

static int loop_transport(void *ctx, const uint8_t *tx, uint32_t tx_len,
                          uint8_t *rx, uint32_t rx_cap, uint32_t *rx_len,
                          uint32_t timeout_ms)
{
    mb_loop_t *loop = (mb_loop_t *)ctx;
    uint32_t out_len = 0;

    (void)timeout_ms;
    if (loop == NULL) {
        return -1;
    }
    if (loop->transport_result != 0) {
        return loop->transport_result;
    }
    if (tx_len > sizeof(loop->req)) {
        return -1;
    }
    memcpy(loop->req, tx, tx_len);
    loop->req_len = tx_len;

    if (modbus_slave_poll(loop->slave, tx, tx_len,
                          loop->resp, sizeof(loop->resp), &out_len) != IOT_OK) {
        return -1;
    }
    if (out_len > rx_cap) {
        return -1;
    }
    memcpy(rx, loop->resp, out_len);
    *rx_len = out_len;
    loop->resp_len = out_len;
    return 0;
}

static uint8_t s_custom_resp[320];
static uint32_t s_custom_len;

static int custom_transport(void *ctx, const uint8_t *tx, uint32_t tx_len,
                            uint8_t *rx, uint32_t rx_cap, uint32_t *rx_len,
                            uint32_t timeout_ms)
{
    (void)ctx;
    (void)tx;
    (void)tx_len;
    (void)timeout_ms;
    if (s_custom_len > rx_cap) {
        return -1;
    }
    memcpy(rx, s_custom_resp, s_custom_len);
    *rx_len = s_custom_len;
    return 0;
}

static void map_fixture(modbus_slave_t *slave, mb_loop_t *loop, modbus_mode_t mode)
{
    memset(s_map_hold, 0, sizeof(s_map_hold));
    memset(s_map_input, 0, sizeof(s_map_input));
    memset(s_map_coil, 0, sizeof(s_map_coil));
    memset(s_map_disc, 0, sizeof(s_map_disc));
    memset(loop, 0, sizeof(*loop));

    modbus_slave_register_init(slave, 1, mode);
    TF_CHECK_INT(modbus_slave_map_regs(slave, 0, s_map_hold,
                                       MAP_REG_COUNT, MODBUS_REG_HOLDING), IOT_OK);
    TF_CHECK_INT(modbus_slave_map_regs(slave, 0, s_map_input,
                                       MAP_REG_COUNT, MODBUS_REG_INPUT), IOT_OK);
    TF_CHECK_INT(modbus_slave_map_bits(slave, 0, s_map_coil,
                                       MAP_REG_COUNT, MODBUS_REG_COIL), IOT_OK);
    TF_CHECK_INT(modbus_slave_map_bits(slave, 0, s_map_disc,
                                       MAP_REG_COUNT, MODBUS_REG_DISCRETE), IOT_OK);

    loop->slave = slave;
}

static void test_map_slave_regs(void)
{
    modbus_slave_t slave;
    mb_loop_t loop;
    modbus_master_t master;
    uint16_t regs[4];
    uint16_t wr[3];

    tf_begin("modbus/映射从站 保持寄存器读写");
    map_fixture(&slave, &loop, MODBUS_RTU);
    TF_CHECK_INT(modbus_master_init(&master, MODBUS_RTU, 1,
                                    loop_transport, &loop, 100), IOT_OK);
    TF_CHECK_INT(modbus_slave_register_range(&slave, NULL), MODBUS_ERR_PARAM);

    s_map_hold[0] = 0x1234;
    s_map_hold[1] = 0xABCD;
    TF_CHECK_INT(modbus_master_read_holding(&master, 0, 2, regs), IOT_OK);
    TF_CHECK_INT(regs[0], 0x1234);
    TF_CHECK_INT(regs[1], 0xABCD);

    TF_CHECK_INT(modbus_master_write_reg(&master, 2, 0x00FF), IOT_OK);
    TF_CHECK_INT(s_map_hold[2], 0x00FF);

    wr[0] = 0x1111;
    wr[1] = 0x2222;
    wr[2] = 0x3333;
    TF_CHECK_INT(modbus_master_write_regs(&master, 4, 3, wr), IOT_OK);
    TF_CHECK_INT(s_map_hold[4], 0x1111);
    TF_CHECK_INT(s_map_hold[5], 0x2222);
    TF_CHECK_INT(s_map_hold[6], 0x3333);

    /* 输入寄存器只读映射 */
    s_map_input[0] = 0x55AA;
    TF_CHECK_INT(modbus_master_read_input(&master, 0, 1, regs), IOT_OK);
    TF_CHECK_INT(regs[0], 0x55AA);
    tf_end();

    tf_begin("modbus/映射从站 线圈与离散输入");
    map_fixture(&slave, &loop, MODBUS_RTU);
    TF_CHECK_INT(modbus_master_init(&master, MODBUS_RTU, 1,
                                    loop_transport, &loop, 100), IOT_OK);
    s_map_coil[0] = 1;
    s_map_coil[3] = 1;
    {
        uint8_t bits[2] = {0, 0};
        TF_CHECK_INT(modbus_master_read_coils(&master, 0, 8, bits), IOT_OK);
        TF_CHECK_INT(bits[0], 0x09);

        TF_CHECK_INT(modbus_master_write_coil(&master, 1, 1), IOT_OK);
        TF_CHECK_INT(s_map_coil[1], 1);
        TF_CHECK_INT(modbus_master_write_coil(&master, 1, 0), IOT_OK);
        TF_CHECK_INT(s_map_coil[1], 0);

        bits[0] = 0x0F;
        TF_CHECK_INT(modbus_master_write_coils(&master, 8, 4, bits), IOT_OK);
        TF_CHECK_INT(s_map_coil[8], 1);
        TF_CHECK_INT(s_map_coil[11], 1);
    }
    s_map_disc[2] = 1;
    {
        uint8_t bits[1] = {0};
        TF_CHECK_INT(modbus_master_read_discrete(&master, 0, 8, bits), IOT_OK);
        TF_CHECK_INT(bits[0], 0x04);
    }
    tf_end();

    tf_begin("modbus/映射从站 回调优先");
    map_fixture(&slave, &loop, MODBUS_RTU);
    TF_CHECK_INT(modbus_master_init(&master, MODBUS_RTU, 1,
                                    loop_transport, &loop, 100), IOT_OK);
    s_cb_reads = 0;
    s_cb_writes = 0;
    s_map_hold[0] = 0xDEAD;
    TF_CHECK_INT(modbus_slave_set_callback(&slave, MODBUS_REG_HOLDING, 0,
                                           map_read_cb, map_write_cb, NULL), IOT_OK);
    TF_CHECK_INT(modbus_master_read_holding(&master, 2, 2, regs), IOT_OK);
    TF_CHECK_INT(regs[0], 20);
    TF_CHECK_INT(regs[1], 21);
    TF_CHECK_INT(s_cb_reads, 1);
    TF_CHECK_INT(s_cb_addr, 2);
    TF_CHECK_INT(s_cb_qty, 2);

    TF_CHECK_INT(modbus_master_write_reg(&master, 5, 0x7777), IOT_OK);
    TF_CHECK_INT(s_cb_writes, 1);
    TF_CHECK_INT(s_cb_addr, 5);
    TF_CHECK_INT(s_cb_qty, 1);
    TF_CHECK_INT(s_cb_values[0], 0x7777);
    TF_CHECK_INT(s_map_hold[5], 0);        /* 回调优先，不写直映射数组 */
    tf_end();
}

static void test_map_slave_errors(void)
{
    modbus_slave_t slave;
    mb_loop_t loop;
    modbus_master_t master;
    uint16_t regs[2];
    uint8_t req[64];
    uint8_t pdu[8];
    uint8_t resp[64];
    uint32_t req_len;
    uint32_t resp_len = 0;

    tf_begin("modbus/映射从站 异常响应");
    map_fixture(&slave, &loop, MODBUS_RTU);
    TF_CHECK_INT(modbus_master_init(&master, MODBUS_RTU, 1,
                                    loop_transport, &loop, 100), IOT_OK);

    TF_CHECK_INT(modbus_master_read_holding(&master, 15, 2, regs),
                 MODBUS_EX_ILLEGAL_DATA_ADDR);
    TF_CHECK_INT(modbus_master_read_holding(&master, 0, 126, regs), MODBUS_ERR_PARAM);
    TF_CHECK_INT(modbus_master_write_reg(&master, 0xFFFF, 1), MODBUS_EX_ILLEGAL_DATA_ADDR);

    TF_CHECK_INT(master.error_count, 2);
    tf_end();

    tf_begin("modbus/映射从站 广播与非法帧");
    map_fixture(&slave, &loop, MODBUS_RTU);
    slave.address = 0;
    pdu[0] = MODBUS_FC_WRITE_SINGLE_REG;
    pdu[1] = 0;
    pdu[2] = 3;
    pdu[3] = 0x12;
    pdu[4] = 0x34;
    req_len = frame_rtu(req, 0, pdu, 5);
    TF_CHECK_INT(modbus_slave_poll(&slave, req, req_len, resp, sizeof(resp), &resp_len),
                 IOT_OK);
    TF_CHECK_INT(resp_len, 0);             /* 广播不回复 */
    TF_CHECK_INT(s_map_hold[3], 0x1234);

    slave.address = 1;
    req_len = frame_rtu(req, 2, pdu, 5);
    TF_CHECK_INT(modbus_slave_poll(&slave, req, req_len, resp, sizeof(resp), &resp_len),
                 MODBUS_ERR_RANGE);

    req[req_len - 1] ^= 0xFF;              /* 破坏 CRC */
    TF_CHECK_INT(modbus_slave_poll(&slave, req, req_len, resp, sizeof(resp), &resp_len),
                 MODBUS_ERR_FRAME);
    tf_end();

    tf_begin("modbus/映射从站 注册表管理");
    modbus_slave_register_init(&slave, 1, MODBUS_RTU);
    TF_CHECK_INT(modbus_slave_map_regs(&slave, 0, NULL, 4, MODBUS_REG_HOLDING),
                 MODBUS_ERR_PARAM);
    TF_CHECK_INT(modbus_slave_map_bits(&slave, 0, NULL, 4, MODBUS_REG_COIL),
                 MODBUS_ERR_PARAM);
    TF_CHECK_INT(modbus_slave_map_regs(&slave, 0, s_map_hold, 0, MODBUS_REG_HOLDING),
                 MODBUS_ERR_PARAM);
    TF_CHECK_INT(modbus_slave_set_callback(&slave, MODBUS_REG_HOLDING, 0,
                                           map_read_cb, NULL, NULL), MODBUS_ERR_RANGE);
    TF_CHECK_INT(modbus_slave_unregister_range(&slave, MODBUS_REG_HOLDING, 0),
                 MODBUS_ERR_RANGE);
    {
        int i;
        for (i = 0; i < MODBUS_SLAVE_MAX_RANGES; i++) {
            TF_CHECK_INT(modbus_slave_map_regs(&slave, (uint16_t)(i * 4),
                                               s_map_hold, 4, MODBUS_REG_HOLDING), IOT_OK);
        }
        TF_CHECK_INT(modbus_slave_map_regs(&slave, 200, s_map_hold, 4,
                                           MODBUS_REG_HOLDING), MODBUS_ERR_NO_MEMORY);
    }
    TF_CHECK_INT(modbus_slave_unregister_range(&slave, MODBUS_REG_HOLDING, 8), IOT_OK);
    TF_CHECK_INT(slave.range_count, MODBUS_SLAVE_MAX_RANGES - 1);
    tf_end();
}

static void test_map_slave_tcp_and_master_errors(void)
{
    modbus_slave_t slave;
    mb_loop_t loop;
    modbus_master_t master;
    uint16_t regs[2];
    uint8_t pdu[8];

    tf_begin("modbus/映射从站 TCP 事务");
    map_fixture(&slave, &loop, MODBUS_TCP);
    TF_CHECK_INT(modbus_master_init(&master, MODBUS_TCP, 1,
                                    loop_transport, &loop, 100), IOT_OK);
    s_map_hold[1] = 0x2468;
    TF_CHECK_INT(modbus_master_read_holding(&master, 0, 2, regs), IOT_OK);
    TF_CHECK_INT(regs[0], 0);
    TF_CHECK_INT(regs[1], 0x2468);
    TF_CHECK_INT(master.transaction_id, 1);
    TF_CHECK_INT(modbus_master_write_reg(&master, 0, 0x1357), IOT_OK);
    TF_CHECK_INT(master.transaction_id, 2);
    TF_CHECK_INT(s_map_hold[0], 0x1357);
    TF_CHECK_INT(loop.req[0], 0x00);
    TF_CHECK_INT(loop.req[1], 0x02);
    tf_end();

    tf_begin("modbus/主站 错误路径");
    map_fixture(&slave, &loop, MODBUS_RTU);
    TF_CHECK_INT(modbus_master_init(&master, MODBUS_RTU, 1,
                                    loop_transport, &loop, 100), IOT_OK);
    TF_CHECK_INT(modbus_master_read_holding(NULL, 0, 1, regs), MODBUS_ERR_PARAM);
    TF_CHECK_INT(modbus_master_read_holding(&master, 0, 0, regs), MODBUS_ERR_PARAM);
    TF_CHECK_INT(modbus_master_read_holding(&master, 0, 1, NULL), MODBUS_ERR_PARAM);

    loop.transport_result = -1;
    TF_CHECK_INT(modbus_master_read_holding(&master, 0, 1, regs),
                 MODBUS_ERR_TRANSPORT);
    loop.transport_result = 0;

    /* 异常响应：FC03 + 0x80, code=02 */
    pdu[0] = (uint8_t)(MODBUS_FC_READ_HOLDING_REGS | 0x80);
    pdu[1] = MODBUS_EX_ILLEGAL_DATA_ADDR;
    s_custom_len = frame_rtu(s_custom_resp, 1, pdu, 2);
    modbus_master_set_transport(&master, custom_transport, NULL);
    TF_CHECK_INT(modbus_master_read_holding(&master, 0, 1, regs),
                 MODBUS_EX_ILLEGAL_DATA_ADDR);

    /* 功能码不匹配：期望 FC03，实际 FC04 */
    pdu[0] = MODBUS_FC_READ_INPUT_REGS;
    pdu[1] = 2;
    pdu[2] = 0x00;
    pdu[3] = 0x11;
    s_custom_len = frame_rtu(s_custom_resp, 1, pdu, 4);
    TF_CHECK_INT(modbus_master_read_holding(&master, 0, 1, regs), MODBUS_ERR_FRAME);
    TF_CHECK(master.error_count > 0);
    tf_end();
}

void ut_modbus(void)
{
    memset(s_regs, 0, sizeof(s_regs));
    memset(s_coils, 0, sizeof(s_coils));
    TF_CHECK_INT(modbus_init(), IOT_OK);
    test_crc_and_build();
    test_parse();
    test_slave_read();
    test_slave_write();
    test_slave_errors();
    test_map_slave_regs();
    test_map_slave_errors();
    test_map_slave_tcp_and_master_errors();
}
