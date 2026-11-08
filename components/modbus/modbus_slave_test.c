/* modbus_slave.c 单元测试：回调从站与寄存器映射从站。 */
#include "test.h"
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

    tf_begin("modbus_slave/FC03 读保持寄存器");
    TF_CHECK_INT(modbus_build_request(MODBUS_RTU, 1, MODBUS_FC_READ_HOLDING_REGS,
                                      0, 2, NULL, 0, req, sizeof(req), &req_len),
                 0);
    TF_CHECK_INT(modbus_slave_process(MODBUS_RTU, 1, &iface, req, req_len,
                                      resp, sizeof(resp), &resp_len), 0);
    TF_CHECK_INT(resp[0], 0x01);
    TF_CHECK_INT(resp[1], 0x03);
    TF_CHECK_INT(modbus_parse_response(MODBUS_RTU, resp, resp_len, &fc,
                                       data, sizeof(data), &dlen), 0);
    TF_CHECK_INT(fc, 0x03);
    TF_CHECK_INT(dlen, 5);
    TF_CHECK_INT(data[0], 4);
    TF_CHECK_INT(data[1], 0x12);
    TF_CHECK_INT(data[4], 0xCD);
    tf_end();

    tf_begin("modbus_slave/FC01/FC02 读线圈与离散输入");
    s_coils[0] = 1;
    s_coils[2] = 1;
    TF_CHECK_INT(modbus_build_request(MODBUS_RTU, 1, MODBUS_FC_READ_COILS,
                                      0, 3, NULL, 0, req, sizeof(req), &req_len),
                 0);
    TF_CHECK_INT(modbus_slave_process(MODBUS_RTU, 1, &iface, req, req_len,
                                      resp, sizeof(resp), &resp_len), 0);
    TF_CHECK_INT(modbus_parse_response(MODBUS_RTU, resp, resp_len, &fc,
                                       data, sizeof(data), &dlen), 0);
    TF_CHECK_INT(dlen, 2);
    TF_CHECK_INT(data[0], 1);
    TF_CHECK_INT(data[1], 0x05);

    TF_CHECK_INT(modbus_build_request(MODBUS_RTU, 1, MODBUS_FC_READ_DISCRETE_INPUTS,
                                      0, 4, NULL, 0, req, sizeof(req), &req_len),
                 0);
    TF_CHECK_INT(modbus_slave_process(MODBUS_RTU, 1, &iface, req, req_len,
                                      resp, sizeof(resp), &resp_len), 0);
    TF_CHECK_INT(modbus_parse_response(MODBUS_RTU, resp, resp_len, &fc,
                                       data, sizeof(data), &dlen), 0);
    TF_CHECK_INT(fc, 0x02);
    TF_CHECK_INT(dlen, 2);
    TF_CHECK_INT(data[1], 0x05);
    tf_end();

    tf_begin("modbus_slave/FC04 读输入寄存器");
    TF_CHECK_INT(modbus_build_request(MODBUS_RTU, 1, MODBUS_FC_READ_INPUT_REGS,
                                      1, 1, NULL, 0, req, sizeof(req), &req_len),
                 0);
    TF_CHECK_INT(modbus_slave_process(MODBUS_RTU, 1, &iface, req, req_len,
                                      resp, sizeof(resp), &resp_len), 0);
    TF_CHECK_INT(modbus_parse_response(MODBUS_RTU, resp, resp_len, &fc,
                                       data, sizeof(data), &dlen), 0);
    TF_CHECK_INT(fc, 0x04);
    TF_CHECK_INT(dlen, 3);
    TF_CHECK_INT(data[1], 0xAB);
    TF_CHECK_INT(data[2], 0xCD);
    tf_end();

    tf_begin("modbus_slave/TCP 读保持寄存器");
    TF_CHECK_INT(modbus_build_request(MODBUS_TCP, 1, MODBUS_FC_READ_HOLDING_REGS,
                                      1, 1, NULL, 0, req, sizeof(req), &req_len),
                 0);
    TF_CHECK_INT(modbus_slave_process(MODBUS_TCP, 1, &iface, req, req_len,
                                      resp, sizeof(resp), &resp_len), 0);
    TF_CHECK_INT(modbus_parse_response(MODBUS_TCP, resp, resp_len, &fc,
                                       data, sizeof(data), &dlen), 0);
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

    tf_begin("modbus_slave/FC05 写单线圈");
    {
        uint8_t val[1] = { 1 };

        TF_CHECK_INT(modbus_build_request(MODBUS_RTU, 1,
                                          MODBUS_FC_WRITE_SINGLE_COIL, 7, 1,
                                          val, 1, req, sizeof(req), &req_len),
                     0);
        TF_CHECK_INT(modbus_slave_process(MODBUS_RTU, 1, &iface, req, req_len,
                                          resp, sizeof(resp), &resp_len),
                     0);
        TF_CHECK_INT(s_coils[7], 1);
        TF_CHECK_INT(modbus_parse_response(MODBUS_RTU, resp, resp_len, &fc,
                                           data, sizeof(data), &dlen), 0);
        TF_CHECK_INT(fc, 0x05);
        TF_CHECK_INT(dlen, 4);
    }
    tf_end();

    tf_begin("modbus_slave/FC06 写单寄存器");
    {
        uint8_t val[2] = { 0x00, 0x2A };

        TF_CHECK_INT(modbus_build_request(MODBUS_RTU, 1,
                                          MODBUS_FC_WRITE_SINGLE_REG, 5, 1,
                                          val, 2, req, sizeof(req), &req_len),
                     0);
        TF_CHECK_INT(modbus_slave_process(MODBUS_RTU, 1, &iface, req, req_len,
                                          resp, sizeof(resp), &resp_len),
                     0);
        TF_CHECK_INT(s_regs[5], 0x002A);
    }
    tf_end();

    tf_begin("modbus_slave/FC0F 写多线圈");
    {
        uint8_t bits[2] = { 0xCD, 0x01 };

        memset(s_coils, 0, sizeof(s_coils));
        TF_CHECK_INT(modbus_build_request(MODBUS_RTU, 1,
                                          MODBUS_FC_WRITE_MULTI_COILS, 0, 10,
                                          bits, 2, req, sizeof(req), &req_len),
                     0);
        TF_CHECK_INT(modbus_slave_process(MODBUS_RTU, 1, &iface, req, req_len,
                                          resp, sizeof(resp), &resp_len),
                     0);
        TF_CHECK_INT(s_coils[0], 1);
        TF_CHECK_INT(s_coils[1], 0);
        TF_CHECK_INT(s_coils[2], 1);
        TF_CHECK_INT(s_coils[3], 1);
        TF_CHECK_INT(s_coils[8], 1);
        TF_CHECK_INT(s_coils[9], 0);
        TF_CHECK_INT(modbus_parse_response(MODBUS_RTU, resp, resp_len, &fc,
                                           data, sizeof(data), &dlen), 0);
        TF_CHECK_INT(fc, 0x0F);
        TF_CHECK_INT(dlen, 4);
        TF_CHECK_INT(data[2], 0x00);
        TF_CHECK_INT(data[3], 0x0A);
    }
    tf_end();

    tf_begin("modbus_slave/FC10 写多寄存器");
    {
        uint8_t vals[4] = { 0x00, 0x11, 0x00, 0x22 };

        TF_CHECK_INT(modbus_build_request(MODBUS_RTU, 1,
                                          MODBUS_FC_WRITE_MULTI_REGS, 0, 2,
                                          vals, 4, req, sizeof(req), &req_len),
                     0);
        TF_CHECK_INT(modbus_slave_process(MODBUS_RTU, 1, &iface, req, req_len,
                                          resp, sizeof(resp), &resp_len),
                     0);
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

    tf_begin("modbus_slave/地址过滤与 CRC 错误");
    TF_CHECK_INT(modbus_build_request(MODBUS_RTU, 2, MODBUS_FC_READ_HOLDING_REGS,
                                      0, 1, NULL, 0, req, sizeof(req), &req_len),
                 0);
    TF_CHECK_INT(modbus_slave_process(MODBUS_RTU, 1, &iface, req, req_len,
                                      resp, sizeof(resp), &resp_len), -1);

    TF_CHECK_INT(modbus_build_request(MODBUS_RTU, 1, MODBUS_FC_READ_HOLDING_REGS,
                                      0, 1, NULL, 0, req, sizeof(req), &req_len),
                 0);
    req[2] ^= 0xFF;
    TF_CHECK_INT(modbus_slave_process(MODBUS_RTU, 1, &iface, req, req_len,
                                      resp, sizeof(resp), &resp_len), -1);
    tf_end();

    tf_begin("modbus_slave/参数非法与容量不足");
    TF_CHECK_INT(modbus_build_request(MODBUS_RTU, 1, MODBUS_FC_READ_HOLDING_REGS,
                                      0, 1, NULL, 0, req, sizeof(req), &req_len),
                 0);
    TF_CHECK_INT(modbus_slave_process(MODBUS_RTU, 1, NULL, req, req_len,
                                      resp, sizeof(resp), &resp_len), -1);
    TF_CHECK_INT(modbus_slave_process(MODBUS_RTU, 1, &iface, req, req_len,
                                      NULL, sizeof(resp), &resp_len), -1);
    TF_CHECK_INT(modbus_slave_process(MODBUS_RTU, 1, &iface, req, req_len,
                                      resp, sizeof(resp), NULL), -1);
    TF_CHECK_INT(modbus_slave_process(MODBUS_RTU, 1, &iface, req, req_len,
                                      resp, 2, &resp_len), -1);
    tf_end();

    tf_begin("modbus_slave/异常响应：非法数据地址");
    TF_CHECK_INT(modbus_build_request(MODBUS_RTU, 1, MODBUS_FC_READ_HOLDING_REGS,
                                      31, 2, NULL, 0, req, sizeof(req), &req_len),
                 0);
    TF_CHECK_INT(modbus_slave_process(MODBUS_RTU, 1, &iface, req, req_len,
                                      resp, sizeof(resp), &resp_len), 0);
    TF_CHECK_INT(resp[1], 0x83);
    TF_CHECK_INT(resp[2], MODBUS_EX_ILLEGAL_DATA_ADDR);
    tf_end();

    tf_begin("modbus_slave/异常响应：数量为 0");
    pdu[0] = MODBUS_FC_READ_COILS;
    pdu[1] = 0x00;
    pdu[2] = 0x00;
    pdu[3] = 0x00;
    pdu[4] = 0x00;
    req_len = frame_rtu(req, 1, pdu, 5);
    TF_CHECK_INT(modbus_slave_process(MODBUS_RTU, 1, &iface, req, req_len,
                                      resp, sizeof(resp), &resp_len), 0);
    TF_CHECK_INT(resp[1], 0x81);
    TF_CHECK_INT(resp[2], MODBUS_EX_ILLEGAL_DATA_VALUE);
    tf_end();

    tf_begin("modbus_slave/异常响应：功能未实现");
    partial.read_coils = NULL;
    pdu[0] = MODBUS_FC_READ_COILS;
    pdu[1] = 0x00;
    pdu[2] = 0x00;
    pdu[3] = 0x00;
    pdu[4] = 0x01;
    req_len = frame_rtu(req, 1, pdu, 5);
    TF_CHECK_INT(modbus_slave_process(MODBUS_RTU, 1, &partial, req, req_len,
                                      resp, sizeof(resp), &resp_len), 0);
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
                                      resp, sizeof(resp), &resp_len), 0);
    TF_CHECK_INT(resp[2], MODBUS_EX_ILLEGAL_FUNCTION);
    tf_end();

    tf_begin("modbus_slave/异常响应：多点写字节数不匹配");
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
                                      resp, sizeof(resp), &resp_len), 0);
    TF_CHECK_INT(resp[1], 0x8F);
    TF_CHECK_INT(resp[2], MODBUS_EX_ILLEGAL_FUNCTION);

    pdu[0] = MODBUS_FC_WRITE_MULTI_REGS;
    pdu[5] = 4;                    /* 4 字节 -> 2 个寄存器，与 qty 10 不符 */
    req_len = frame_rtu(req, 1, pdu, 10);
    TF_CHECK_INT(modbus_slave_process(MODBUS_RTU, 1, &partial, req, req_len,
                                      resp, sizeof(resp), &resp_len), 0);
    TF_CHECK_INT(resp[1], 0x90);
    TF_CHECK_INT(resp[2], MODBUS_EX_ILLEGAL_FUNCTION);
    tf_end();

    tf_begin("modbus_slave/未知功能码与过短 PDU");
    pdu[0] = 0x63;
    pdu[1] = 0x00;
    pdu[2] = 0x00;
    req_len = frame_rtu(req, 1, pdu, 2);
    TF_CHECK_INT(modbus_slave_process(MODBUS_RTU, 1, &iface, req, req_len,
                                      resp, sizeof(resp), &resp_len), 0);
    TF_CHECK_INT(resp[1], 0xE3);
    TF_CHECK_INT(resp[2], MODBUS_EX_ILLEGAL_FUNCTION);

    pdu[0] = MODBUS_FC_READ_COILS;
    req_len = frame_rtu(req, 1, pdu, 1);
    TF_CHECK_INT(modbus_slave_process(MODBUS_RTU, 1, &iface, req, req_len,
                                      resp, sizeof(resp), &resp_len), -1);
    tf_end();

    tf_begin("modbus_slave/TCP 地址过滤");
    TF_CHECK_INT(modbus_build_request(MODBUS_TCP, 7, MODBUS_FC_READ_HOLDING_REGS,
                                      0, 1, NULL, 0, req, sizeof(req), &req_len),
                 0);
    TF_CHECK_INT(modbus_slave_process(MODBUS_TCP, 1, &iface, req, req_len,
                                      resp, sizeof(resp), &resp_len), -1);
    tf_end();
}

/* ================= 寄存器映射从站 ================= */
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
                          loop->resp, sizeof(loop->resp), &out_len) != 0) {
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
                                       MAP_REG_COUNT, MODBUS_REG_HOLDING), 0);
    TF_CHECK_INT(modbus_slave_map_regs(slave, 0, s_map_input,
                                       MAP_REG_COUNT, MODBUS_REG_INPUT), 0);
    TF_CHECK_INT(modbus_slave_map_bits(slave, 0, s_map_coil,
                                       MAP_REG_COUNT, MODBUS_REG_COIL), 0);
    TF_CHECK_INT(modbus_slave_map_bits(slave, 0, s_map_disc,
                                       MAP_REG_COUNT, MODBUS_REG_DISCRETE), 0);

    loop->slave = slave;
}

static void test_map_slave_regs(void)
{
    modbus_slave_t slave;
    mb_loop_t loop;
    modbus_master_t master;
    uint16_t regs[4];
    uint16_t wr[3];

    tf_begin("modbus_slave/映射从站 保持寄存器读写");
    map_fixture(&slave, &loop, MODBUS_RTU);
    TF_CHECK_INT(modbus_master_init(&master, MODBUS_RTU, 1,
                                    loop_transport, &loop, 100), 0);
    TF_CHECK_INT(modbus_slave_register_range(&slave, NULL), MODBUS_ERR_PARAM);

    s_map_hold[0] = 0x1234;
    s_map_hold[1] = 0xABCD;
    TF_CHECK_INT(modbus_master_read_holding(&master, 0, 2, regs), 0);
    TF_CHECK_INT(regs[0], 0x1234);
    TF_CHECK_INT(regs[1], 0xABCD);

    TF_CHECK_INT(modbus_master_write_reg(&master, 2, 0x00FF), 0);
    TF_CHECK_INT(s_map_hold[2], 0x00FF);

    wr[0] = 0x1111;
    wr[1] = 0x2222;
    wr[2] = 0x3333;
    TF_CHECK_INT(modbus_master_write_regs(&master, 4, 3, wr), 0);
    TF_CHECK_INT(s_map_hold[4], 0x1111);
    TF_CHECK_INT(s_map_hold[5], 0x2222);
    TF_CHECK_INT(s_map_hold[6], 0x3333);

    s_map_input[0] = 0x55AA;
    TF_CHECK_INT(modbus_master_read_input(&master, 0, 1, regs), 0);
    TF_CHECK_INT(regs[0], 0x55AA);
    tf_end();

    tf_begin("modbus_slave/映射从站 线圈与离散输入");
    map_fixture(&slave, &loop, MODBUS_RTU);
    TF_CHECK_INT(modbus_master_init(&master, MODBUS_RTU, 1,
                                    loop_transport, &loop, 100), 0);
    s_map_coil[0] = 1;
    s_map_coil[3] = 1;
    {
        uint8_t bits[2] = { 0, 0 };

        TF_CHECK_INT(modbus_master_read_coils(&master, 0, 8, bits), 0);
        TF_CHECK_INT(bits[0], 0x09);

        TF_CHECK_INT(modbus_master_write_coil(&master, 1, 1), 0);
        TF_CHECK_INT(s_map_coil[1], 1);
        TF_CHECK_INT(modbus_master_write_coil(&master, 1, 0), 0);
        TF_CHECK_INT(s_map_coil[1], 0);

        bits[0] = 0x0F;
        TF_CHECK_INT(modbus_master_write_coils(&master, 8, 4, bits), 0);
        TF_CHECK_INT(s_map_coil[8], 1);
        TF_CHECK_INT(s_map_coil[11], 1);
    }
    s_map_disc[2] = 1;
    {
        uint8_t bits[1] = { 0 };

        TF_CHECK_INT(modbus_master_read_discrete(&master, 0, 8, bits), 0);
        TF_CHECK_INT(bits[0], 0x04);
    }
    tf_end();

    tf_begin("modbus_slave/映射从站 回调优先");
    map_fixture(&slave, &loop, MODBUS_RTU);
    TF_CHECK_INT(modbus_master_init(&master, MODBUS_RTU, 1,
                                    loop_transport, &loop, 100), 0);
    s_cb_reads = 0;
    s_cb_writes = 0;
    s_map_hold[0] = 0xDEAD;
    TF_CHECK_INT(modbus_slave_set_callback(&slave, MODBUS_REG_HOLDING, 0,
                                           map_read_cb, map_write_cb, NULL), 0);
    TF_CHECK_INT(modbus_master_read_holding(&master, 2, 2, regs), 0);
    TF_CHECK_INT(regs[0], 20);
    TF_CHECK_INT(regs[1], 21);
    TF_CHECK_INT(s_cb_reads, 1);
    TF_CHECK_INT(s_cb_addr, 2);
    TF_CHECK_INT(s_cb_qty, 2);

    TF_CHECK_INT(modbus_master_write_reg(&master, 5, 0x7777), 0);
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

    tf_begin("modbus_slave/映射从站 异常响应");
    map_fixture(&slave, &loop, MODBUS_RTU);
    TF_CHECK_INT(modbus_master_init(&master, MODBUS_RTU, 1,
                                    loop_transport, &loop, 100), 0);

    TF_CHECK_INT(modbus_master_read_holding(&master, 15, 2, regs),
                 MODBUS_EX_ILLEGAL_DATA_ADDR);
    TF_CHECK_INT(modbus_master_read_holding(&master, 0, 126, regs),
                 MODBUS_ERR_PARAM);
    TF_CHECK_INT(modbus_master_write_reg(&master, 0xFFFF, 1),
                 MODBUS_EX_ILLEGAL_DATA_ADDR);

    TF_CHECK_INT(master.error_count, 2);
    tf_end();

    tf_begin("modbus_slave/映射从站 广播与非法帧");
    map_fixture(&slave, &loop, MODBUS_RTU);
    slave.address = 0;
    pdu[0] = MODBUS_FC_WRITE_SINGLE_REG;
    pdu[1] = 0;
    pdu[2] = 3;
    pdu[3] = 0x12;
    pdu[4] = 0x34;
    req_len = frame_rtu(req, 0, pdu, 5);
    TF_CHECK_INT(modbus_slave_poll(&slave, req, req_len, resp, sizeof(resp),
                                   &resp_len), 0);
    TF_CHECK_INT(resp_len, 0);             /* 广播不回复 */
    TF_CHECK_INT(s_map_hold[3], 0x1234);

    slave.address = 1;
    req_len = frame_rtu(req, 2, pdu, 5);
    TF_CHECK_INT(modbus_slave_poll(&slave, req, req_len, resp, sizeof(resp),
                                   &resp_len), MODBUS_ERR_RANGE);

    req[req_len - 1] ^= 0xFF;              /* 破坏 CRC */
    TF_CHECK_INT(modbus_slave_poll(&slave, req, req_len, resp, sizeof(resp),
                                   &resp_len), MODBUS_ERR_FRAME);
    tf_end();

    tf_begin("modbus_slave/映射从站 注册表管理");
    modbus_slave_register_init(&slave, 1, MODBUS_RTU);
    TF_CHECK_INT(modbus_slave_map_regs(&slave, 0, NULL, 4,
                                       MODBUS_REG_HOLDING), MODBUS_ERR_PARAM);
    TF_CHECK_INT(modbus_slave_map_bits(&slave, 0, NULL, 4,
                                       MODBUS_REG_COIL), MODBUS_ERR_PARAM);
    TF_CHECK_INT(modbus_slave_map_regs(&slave, 0, s_map_hold, 0,
                                       MODBUS_REG_HOLDING), MODBUS_ERR_PARAM);
    TF_CHECK_INT(modbus_slave_set_callback(&slave, MODBUS_REG_HOLDING, 0,
                                           map_read_cb, NULL, NULL),
                 MODBUS_ERR_RANGE);
    TF_CHECK_INT(modbus_slave_unregister_range(&slave, MODBUS_REG_HOLDING, 0),
                 MODBUS_ERR_RANGE);
    {
        int i;

        for (i = 0; i < MODBUS_SLAVE_MAX_RANGES; i++) {
            TF_CHECK_INT(modbus_slave_map_regs(&slave, (uint16_t)(i * 4),
                                               s_map_hold, 4,
                                               MODBUS_REG_HOLDING), 0);
        }
        TF_CHECK_INT(modbus_slave_map_regs(&slave, 200, s_map_hold, 4,
                                           MODBUS_REG_HOLDING),
                     MODBUS_ERR_NO_MEMORY);
    }
    TF_CHECK_INT(modbus_slave_unregister_range(&slave, MODBUS_REG_HOLDING, 8),
                 0);
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

    tf_begin("modbus_slave/映射从站 TCP 事务");
    map_fixture(&slave, &loop, MODBUS_TCP);
    TF_CHECK_INT(modbus_master_init(&master, MODBUS_TCP, 1,
                                    loop_transport, &loop, 100), 0);
    s_map_hold[1] = 0x2468;
    TF_CHECK_INT(modbus_master_read_holding(&master, 0, 2, regs), 0);
    TF_CHECK_INT(regs[0], 0);
    TF_CHECK_INT(regs[1], 0x2468);
    TF_CHECK_INT(master.transaction_id, 1);
    TF_CHECK_INT(modbus_master_write_reg(&master, 0, 0x1357), 0);
    TF_CHECK_INT(master.transaction_id, 2);
    TF_CHECK_INT(s_map_hold[0], 0x1357);
    TF_CHECK_INT(loop.req[0], 0x00);
    TF_CHECK_INT(loop.req[1], 0x02);
    tf_end();

    tf_begin("modbus_slave/主站异常与错误路径");
    map_fixture(&slave, &loop, MODBUS_RTU);
    TF_CHECK_INT(modbus_master_init(&master, MODBUS_RTU, 1,
                                    loop_transport, &loop, 100), 0);
    TF_CHECK_INT(modbus_master_read_holding(NULL, 0, 1, regs), MODBUS_ERR_PARAM);
    TF_CHECK_INT(modbus_master_read_holding(&master, 0, 0, regs),
                 MODBUS_ERR_PARAM);
    TF_CHECK_INT(modbus_master_read_holding(&master, 0, 1, NULL),
                 MODBUS_ERR_PARAM);

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
    TF_CHECK_INT(modbus_master_read_holding(&master, 0, 1, regs),
                 MODBUS_ERR_FRAME);
    TF_CHECK(master.error_count > 0);
    tf_end();
}

void ut_modbus_slave(void)
{
    memset(s_regs, 0, sizeof(s_regs));
    memset(s_coils, 0, sizeof(s_coils));
    TF_CHECK_INT(modbus_init(), 0);
    test_slave_read();
    test_slave_write();
    test_slave_errors();
    test_map_slave_regs();
    test_map_slave_errors();
    test_map_slave_tcp_and_master_errors();
}
