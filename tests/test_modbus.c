/* Modbus RTU/TCP 主站与从站测试。 */
#include "test_framework.h"
#include "modbus.h"

#include <string.h>

#define TEST_REG_COUNT  16
#define TEST_COIL_COUNT 16

static uint16_t g_regs[TEST_REG_COUNT];
static uint8_t g_coils[TEST_COIL_COUNT];

static int test_read_bits(uint16_t addr, uint16_t qty, uint8_t *bits, void *user)
{
    uint32_t i;
    (void)user;
    if ((uint32_t)addr + qty > TEST_COIL_COUNT) {
        return -1;
    }
    memset(bits, 0, (qty + 7u) / 8u);
    for (i = 0; i < qty; i++) {
        if (g_coils[addr + i]) {
            bits[i / 8u] |= (uint8_t)(1u << (i % 8u));
        }
    }
    return 0;
}

static int test_read_regs(uint16_t addr, uint16_t qty, uint16_t *regs, void *user)
{
    uint32_t i;
    (void)user;
    if ((uint32_t)addr + qty > TEST_REG_COUNT) {
        return -1;
    }
    for (i = 0; i < qty; i++) {
        regs[i] = g_regs[addr + i];
    }
    return 0;
}

static int test_write_bits(uint16_t addr, const uint8_t *bits, uint16_t qty, void *user)
{
    uint32_t i;
    (void)user;
    if ((uint32_t)addr + qty > TEST_COIL_COUNT) {
        return -1;
    }
    for (i = 0; i < qty; i++) {
        g_coils[addr + i] = (uint8_t)((bits[i / 8u] >> (i % 8u)) & 1u);
    }
    return 0;
}

static int test_write_regs(uint16_t addr, const uint16_t *regs, uint16_t qty, void *user)
{
    uint32_t i;
    (void)user;
    if ((uint32_t)addr + qty > TEST_REG_COUNT) {
        return -1;
    }
    for (i = 0; i < qty; i++) {
        g_regs[addr + i] = regs[i];
    }
    return 0;
}

static modbus_slave_if_t make_iface(void)
{
    modbus_slave_if_t iface;
    iface.read_coils = test_read_bits;
    iface.read_discrete = test_read_bits;
    iface.read_holding = test_read_regs;
    iface.read_input = test_read_regs;
    iface.write_coils = test_write_bits;
    iface.write_regs = test_write_regs;
    iface.user = NULL;
    return iface;
}

static void test_build(void)
{
    uint8_t adu[64];
    uint32_t len = 0;

    tf_begin("modbus/crc16");
    TF_CHECK_INT(modbus_crc16((const uint8_t *)"123456789", 9), 0x4B37);
    tf_end();

    tf_begin("modbus/RTU 请求组帧");
    TF_CHECK_INT(modbus_build_request(MODBUS_RTU, 1, MODBUS_FC_READ_HOLDING_REGS,
                                      0x0000, 2, NULL, 0, adu, sizeof(adu), &len),
                 IOT_OK);
    TF_CHECK_INT(len, 8);
    TF_CHECK_INT(adu[0], 0x01);
    TF_CHECK_INT(adu[1], 0x03);
    TF_CHECK_INT(adu[2], 0x00);
    TF_CHECK_INT(adu[3], 0x00);
    TF_CHECK_INT(adu[4], 0x00);
    TF_CHECK_INT(adu[5], 0x02);
    /* 末尾 CRC 低字节在前 */
    {
        uint16_t crc = modbus_crc16(adu, 6);
        TF_CHECK_INT(adu[6], crc & 0xFF);
        TF_CHECK_INT(adu[7], (crc >> 8) & 0xFF);
    }
    tf_end();

    tf_begin("modbus/TCP 请求组帧");
    TF_CHECK_INT(modbus_build_request(MODBUS_TCP, 1, MODBUS_FC_READ_HOLDING_REGS,
                                      0x0000, 2, NULL, 0, adu, sizeof(adu), &len),
                 IOT_OK);
    TF_CHECK_INT(len, 12);
    TF_CHECK_INT(adu[0], 0x00);
    TF_CHECK_INT(adu[1], 0x01);   /* transaction id */
    TF_CHECK_INT(adu[2], 0x00);
    TF_CHECK_INT(adu[3], 0x00);   /* protocol id */
    TF_CHECK_INT(adu[4], 0x00);
    TF_CHECK_INT(adu[5], 0x06);   /* unit + PDU 长度 */
    TF_CHECK_INT(adu[6], 0x01);   /* unit id */
    TF_CHECK_INT(adu[7], 0x03);
    tf_end();

    tf_begin("modbus/请求参数校验");
    TF_CHECK_INT(modbus_build_request(MODBUS_RTU, 1, MODBUS_FC_READ_HOLDING_REGS,
                                      0, 0, NULL, 0, adu, sizeof(adu), &len),
                 -1);
    TF_CHECK_INT(modbus_build_request(MODBUS_RTU, 1, 0x63,
                                      0, 1, NULL, 0, adu, sizeof(adu), &len),
                 -1);
    TF_CHECK_INT(modbus_build_request(MODBUS_RTU, 1, MODBUS_FC_READ_HOLDING_REGS,
                                      0, 2, NULL, 0, NULL, 0, &len),
                 -1);
    tf_end();
}

static void test_slave_read(void)
{
    modbus_slave_if_t iface = make_iface();
    uint8_t req[64];
    uint8_t resp[64];
    uint32_t req_len = 0, resp_len = 0, data_len = 0;
    uint8_t fc = 0;
    uint8_t data[32];

    g_regs[0] = 0x1234;
    g_regs[1] = 0xABCD;

    tf_begin("modbus/从站 FC03 读保持寄存器");
    modbus_build_request(MODBUS_RTU, 1, MODBUS_FC_READ_HOLDING_REGS,
                         0, 2, NULL, 0, req, sizeof(req), &req_len);
    TF_CHECK_INT(modbus_slave_process(MODBUS_RTU, 1, &iface, req, req_len,
                                      resp, sizeof(resp), &resp_len), IOT_OK);
    TF_CHECK_INT(resp[0], 0x01);
    TF_CHECK_INT(resp[1], 0x03);
    TF_CHECK_INT(modbus_parse_response(MODBUS_RTU, resp, resp_len, &fc,
                                       data, sizeof(data), &data_len), IOT_OK);
    TF_CHECK_INT(fc, 0x03);
    TF_CHECK_INT(data_len, 5);       /* 字节数 + 4 字节数据 */
    TF_CHECK_INT(data[0], 4);
    TF_CHECK_INT(data[1], 0x12);
    TF_CHECK_INT(data[2], 0x34);
    TF_CHECK_INT(data[3], 0xAB);
    TF_CHECK_INT(data[4], 0xCD);
    tf_end();

    tf_begin("modbus/从站 FC01 读线圈");
    g_coils[0] = 1;
    g_coils[1] = 0;
    g_coils[2] = 1;
    modbus_build_request(MODBUS_RTU, 1, MODBUS_FC_READ_COILS,
                         0, 3, NULL, 0, req, sizeof(req), &req_len);
    TF_CHECK_INT(modbus_slave_process(MODBUS_RTU, 1, &iface, req, req_len,
                                      resp, sizeof(resp), &resp_len), IOT_OK);
    TF_CHECK_INT(modbus_parse_response(MODBUS_RTU, resp, resp_len, &fc,
                                       data, sizeof(data), &data_len), IOT_OK);
    TF_CHECK_INT(data_len, 2);
    TF_CHECK_INT(data[0], 1);        /* 字节数 */
    TF_CHECK_INT(data[1], 0x05);     /* bit0=1 bit2=1 */
    tf_end();

    tf_begin("modbus/从站异常响应");
    modbus_build_request(MODBUS_RTU, 1, MODBUS_FC_READ_HOLDING_REGS,
                         15, 2, NULL, 0, req, sizeof(req), &req_len);
    TF_CHECK_INT(modbus_slave_process(MODBUS_RTU, 1, &iface, req, req_len,
                                      resp, sizeof(resp), &resp_len), IOT_OK);
    TF_CHECK_INT(modbus_parse_response(MODBUS_RTU, resp, resp_len, &fc,
                                       data, sizeof(data), &data_len),
                 MODBUS_EX_ILLEGAL_DATA_ADDR);
    TF_CHECK_INT(fc, 0x03);
    tf_end();

    tf_begin("modbus/TCP 从站读寄存器");
    g_regs[3] = 0x00FF;
    modbus_build_request(MODBUS_TCP, 1, MODBUS_FC_READ_HOLDING_REGS,
                         3, 1, NULL, 0, req, sizeof(req), &req_len);
    TF_CHECK_INT(modbus_slave_process(MODBUS_TCP, 1, &iface, req, req_len,
                                      resp, sizeof(resp), &resp_len), IOT_OK);
    TF_CHECK_INT(modbus_parse_response(MODBUS_TCP, resp, resp_len, &fc,
                                       data, sizeof(data), &data_len), IOT_OK);
    TF_CHECK_INT(fc, 0x03);
    TF_CHECK_INT(data[0], 2);
    TF_CHECK_INT(data[1], 0x00);
    TF_CHECK_INT(data[2], 0xFF);
    tf_end();
}

static void test_slave_write(void)
{
    modbus_slave_if_t iface = make_iface();
    uint8_t req[64];
    uint8_t resp[64];
    uint32_t req_len = 0, resp_len = 0, data_len = 0;
    uint8_t fc = 0;
    uint8_t data[32];

    tf_begin("modbus/从站 FC06 写单寄存器");
    {
        uint8_t val[2] = { 0x00, 0x2A };
        g_regs[5] = 0;
        modbus_build_request(MODBUS_RTU, 1, MODBUS_FC_WRITE_SINGLE_REG,
                             5, 1, val, sizeof(val), req, sizeof(req), &req_len);
        TF_CHECK_INT(modbus_slave_process(MODBUS_RTU, 1, &iface, req, req_len,
                                          resp, sizeof(resp), &resp_len), IOT_OK);
        TF_CHECK_INT(g_regs[5], 0x002A);
        TF_CHECK_INT(modbus_parse_response(MODBUS_RTU, resp, resp_len, &fc,
                                           data, sizeof(data), &data_len), IOT_OK);
        TF_CHECK_INT(fc, 0x06);
        TF_CHECK_INT(data_len, 4);
        TF_CHECK_INT(data[0], 0x00);
        TF_CHECK_INT(data[1], 0x05);
        TF_CHECK_INT(data[3], 0x2A);
    }
    tf_end();

    tf_begin("modbus/从站 FC10 写多寄存器");
    {
        uint8_t vals[4] = { 0x00, 0x11, 0x00, 0x22 };
        g_regs[0] = 0;
        g_regs[1] = 0;
        modbus_build_request(MODBUS_RTU, 1, MODBUS_FC_WRITE_MULTI_REGS,
                             0, 2, vals, sizeof(vals), req, sizeof(req), &req_len);
        TF_CHECK_INT(modbus_slave_process(MODBUS_RTU, 1, &iface, req, req_len,
                                          resp, sizeof(resp), &resp_len), IOT_OK);
        TF_CHECK_INT(g_regs[0], 0x0011);
        TF_CHECK_INT(g_regs[1], 0x0022);
        TF_CHECK_INT(modbus_parse_response(MODBUS_RTU, resp, resp_len, &fc,
                                           data, sizeof(data), &data_len), IOT_OK);
        TF_CHECK_INT(fc, 0x10);
        TF_CHECK_INT(data_len, 4);
        TF_CHECK_INT(data[3], 0x02);
    }
    tf_end();

    tf_begin("modbus/从站 FC05 写单线圈");
    {
        uint8_t val[1] = { 1 };
        g_coils[7] = 0;
        modbus_build_request(MODBUS_RTU, 1, MODBUS_FC_WRITE_SINGLE_COIL,
                             7, 1, val, sizeof(val), req, sizeof(req), &req_len);
        TF_CHECK_INT(modbus_slave_process(MODBUS_RTU, 1, &iface, req, req_len,
                                          resp, sizeof(resp), &resp_len), IOT_OK);
        TF_CHECK_INT(g_coils[7], 1);
    }
    tf_end();
}

static void test_error_paths(void)
{
    modbus_slave_if_t iface = make_iface();
    uint8_t req[64];
    uint8_t resp[64];
    uint32_t req_len = 0, resp_len = 0;

    tf_begin("modbus/地址不匹配与 CRC 错误");
    modbus_build_request(MODBUS_RTU, 2, MODBUS_FC_READ_HOLDING_REGS,
                         0, 1, NULL, 0, req, sizeof(req), &req_len);
    TF_CHECK_INT(modbus_slave_process(MODBUS_RTU, 1, &iface, req, req_len,
                                      resp, sizeof(resp), &resp_len),
                 -1);

    modbus_build_request(MODBUS_RTU, 1, MODBUS_FC_READ_HOLDING_REGS,
                         0, 1, NULL, 0, req, sizeof(req), &req_len);
    req[2] ^= 0xFF;
    TF_CHECK_INT(modbus_slave_process(MODBUS_RTU, 1, &iface, req, req_len,
                                      resp, sizeof(resp), &resp_len),
                 -1);
    tf_end();

    tf_begin("modbus/解析非法帧");
    {
        uint32_t data_len = 0;
        uint8_t fc = 0;
        uint8_t data[8];
        static const uint8_t too_short[3] = { 1, 3, 0 };
        TF_CHECK_INT(modbus_parse_response(MODBUS_RTU, too_short, 3, &fc,
                                           data, sizeof(data), &data_len),
                     -1);
    }
    tf_end();

    tf_begin("modbus/空接口参数");
    TF_CHECK_INT(modbus_slave_process(MODBUS_RTU, 1, NULL, req, req_len,
                                      resp, sizeof(resp), &resp_len),
                 -1);
    tf_end();
}

void test_modbus(void)
{
    memset(g_regs, 0, sizeof(g_regs));
    memset(g_coils, 0, sizeof(g_coils));
    test_build();
    test_slave_read();
    test_slave_write();
    test_error_paths();
}
