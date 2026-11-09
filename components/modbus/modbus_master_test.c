/* modbus_master.c 单元测试：初始化、事务、读写与错误路径。 */
#include "test.h"
#include "modbus.h"

#include <string.h>

static uint8_t g_resp[320];
static uint32_t g_resp_len;
static uint8_t g_req[320];
static uint32_t g_req_len;
static int g_transport_fail;

static int scripted_transport(void *ctx, const uint8_t *tx, uint32_t tx_len,
                              uint8_t *rx, uint32_t rx_cap, uint32_t *rx_len,
                              uint32_t timeout_ms)
{
    (void)ctx;
    (void)timeout_ms;

    if (g_transport_fail) {
        return -1;
    }
    if (tx_len <= sizeof(g_req)) {
        memcpy(g_req, tx, tx_len);
        g_req_len = tx_len;
    }
    if (g_resp_len > rx_cap) {
        return -1;
    }
    memcpy(rx, g_resp, g_resp_len);
    *rx_len = g_resp_len;
    return 0;
}

static void set_response(modbus_mode_e mode, uint8_t slave,
                         const uint8_t *pdu, uint32_t n)
{
    g_resp_len = modbus_frame_adu(mode, slave, pdu, n, g_resp, sizeof(g_resp));
}

static void test_init_params(void)
{
    modbus_master_t master;
    uint16_t regs[2];
    uint8_t bits[4] = { 0 };

    tf_begin("modbus_master/初始化与参数校验");
    TF_CHECK_INT(modbus_master_init(NULL, MODBUS_RTU, 1, scripted_transport,
                                    NULL, 100), MODBUS_ERR_PARAM);
    TF_CHECK_INT(modbus_master_init(&master, MODBUS_RTU, 1, scripted_transport,
                                    NULL, 100), 0);
    TF_CHECK_INT(master.mode, MODBUS_RTU);
    TF_CHECK_INT(master.address, 1);
    TF_CHECK_INT(master.request_count, 0);
    TF_CHECK_INT(master.error_count, 0);
    TF_CHECK_INT(master.transaction_id, 0);
    modbus_master_set_address(&master, 7);
    TF_CHECK_INT(master.address, 7);
    modbus_master_set_address(NULL, 1);
    modbus_master_set_transport(NULL, scripted_transport, NULL);
    modbus_master_set_transport(&master, scripted_transport, NULL);

    TF_CHECK_INT(modbus_master_read_holding(NULL, 0, 1, regs),
                 MODBUS_ERR_PARAM);
    TF_CHECK_INT(modbus_master_read_holding(&master, 0, 0, regs),
                 MODBUS_ERR_PARAM);
    TF_CHECK_INT(modbus_master_read_holding(&master, 0, 1, NULL),
                 MODBUS_ERR_PARAM);
    TF_CHECK_INT(modbus_master_read_holding(&master, 0, 126, regs),
                 MODBUS_ERR_PARAM);
    TF_CHECK_INT(modbus_master_read_coils(&master, 0, 0, bits),
                 MODBUS_ERR_PARAM);
    TF_CHECK_INT(modbus_master_read_coils(&master, 0, 1, NULL),
                 MODBUS_ERR_PARAM);
    TF_CHECK_INT(modbus_master_write_coils(&master, 0, 0, bits),
                 MODBUS_ERR_PARAM);
    TF_CHECK_INT(modbus_master_write_regs(&master, 0, 124, regs),
                 MODBUS_ERR_PARAM);
    tf_end();

    tf_begin("modbus_master/未设置 transport");
    TF_CHECK_INT(modbus_master_init(&master, MODBUS_RTU, 1, NULL, NULL, 100),
                 0);
    TF_CHECK_INT(modbus_master_read_holding(&master, 0, 1, regs),
                 MODBUS_ERR_PARAM);
    tf_end();
}

static void test_reads(void)
{
    modbus_master_t master;
    uint16_t regs[2];
    uint8_t bits[4];
    uint8_t pdu[16];

    tf_begin("modbus_master/读保持与输入寄存器");
    g_transport_fail = 0;
    g_resp_len = 0;
    g_req_len = 0;
    TF_CHECK_INT(modbus_master_init(&master, MODBUS_RTU, 1,
                                    scripted_transport, NULL, 100), 0);
    pdu[0] = MODBUS_FC_READ_HOLDING_REGS;
    pdu[1] = 4;
    pdu[2] = 0x12;
    pdu[3] = 0x34;
    pdu[4] = 0xAB;
    pdu[5] = 0xCD;
    set_response(MODBUS_RTU, 1, pdu, 6);
    TF_CHECK_INT(modbus_master_read_holding(&master, 0x10, 2, regs), 0);
    TF_CHECK_INT(regs[0], 0x1234);
    TF_CHECK_INT(regs[1], 0xABCD);
    TF_CHECK_INT(master.request_count, 1);
    TF_CHECK_INT((int)g_req_len, 8);
    TF_CHECK_INT(g_req[0], 1);
    TF_CHECK_INT(g_req[1], MODBUS_FC_READ_HOLDING_REGS);
    TF_CHECK_INT(g_req[3], 0x10);
    TF_CHECK_INT(g_req[5], 2);

    pdu[0] = MODBUS_FC_READ_INPUT_REGS;
    pdu[1] = 2;
    pdu[2] = 0x00;
    pdu[3] = 0x7B;
    set_response(MODBUS_RTU, 1, pdu, 4);
    TF_CHECK_INT(modbus_master_read_input(&master, 1, 1, regs), 0);
    TF_CHECK_INT(regs[0], 0x7B);
    tf_end();

    tf_begin("modbus_master/读线圈与离散输入");
    pdu[0] = MODBUS_FC_READ_COILS;
    pdu[1] = 2;
    pdu[2] = 0x09;
    pdu[3] = 0x00;
    set_response(MODBUS_RTU, 1, pdu, 4);
    TF_CHECK_INT(modbus_master_read_coils(&master, 0, 10, bits), 0);
    TF_CHECK_INT(bits[0], 0x09);
    TF_CHECK_INT(bits[1], 0x00);

    pdu[0] = MODBUS_FC_READ_DISCRETE_INPUTS;
    pdu[1] = 1;
    pdu[2] = 0x04;
    set_response(MODBUS_RTU, 1, pdu, 3);
    TF_CHECK_INT(modbus_master_read_discrete(&master, 0, 3, bits), 0);
    TF_CHECK_INT(bits[0], 0x04);

    /* 字节数不符 / 数据不足 */
    pdu[0] = MODBUS_FC_READ_COILS;
    pdu[1] = 1;
    pdu[2] = 0x01;
    set_response(MODBUS_RTU, 1, pdu, 3);
    TF_CHECK_INT(modbus_master_read_coils(&master, 0, 10, bits),
                 MODBUS_ERR_FRAME);

    pdu[0] = MODBUS_FC_READ_HOLDING_REGS;
    pdu[1] = 2;
    pdu[2] = 0x00;
    set_response(MODBUS_RTU, 1, pdu, 3);
    TF_CHECK_INT(modbus_master_read_holding(&master, 0, 1, regs),
                 MODBUS_ERR_FRAME);
    tf_end();
}

static void test_writes(void)
{
    modbus_master_t master;
    uint8_t pdu[16];
    uint8_t bits[2];
    uint16_t regs[3];

    tf_begin("modbus_master/写单线圈与单寄存器");
    g_transport_fail = 0;
    g_resp_len = 0;
    TF_CHECK_INT(modbus_master_init(&master, MODBUS_RTU, 1,
                                    scripted_transport, NULL, 100), 0);

    pdu[0] = MODBUS_FC_WRITE_SINGLE_COIL;
    pdu[1] = 0x00;
    pdu[2] = 0x05;
    pdu[3] = 0xFF;
    pdu[4] = 0x00;
    set_response(MODBUS_RTU, 1, pdu, 5);
    TF_CHECK_INT(modbus_master_write_coil(&master, 5, 1), 0);
    TF_CHECK_INT(g_req[1], MODBUS_FC_WRITE_SINGLE_COIL);
    TF_CHECK_INT(g_req[3], 0x05);
    TF_CHECK_INT(g_req[4], 0xFF);
    TF_CHECK_INT(g_req[5], 0x00);
    pdu[3] = 0x00;                 /* 关闭线圈的回显 */
    set_response(MODBUS_RTU, 1, pdu, 5);
    TF_CHECK_INT(modbus_master_write_coil(&master, 5, 0), 0);
    TF_CHECK_INT(g_req[4], 0x00);
    TF_CHECK_INT(g_req[5], 0x00);

    /* 回显值不匹配 */
    pdu[3] = 0x00;
    set_response(MODBUS_RTU, 1, pdu, 5);
    TF_CHECK_INT(modbus_master_write_coil(&master, 5, 1), MODBUS_ERR_FRAME);

    pdu[0] = MODBUS_FC_WRITE_SINGLE_REG;
    pdu[1] = 0x00;
    pdu[2] = 0x02;
    pdu[3] = 0x12;
    pdu[4] = 0x34;
    set_response(MODBUS_RTU, 1, pdu, 5);
    TF_CHECK_INT(modbus_master_write_reg(&master, 2, 0x1234), 0);
    TF_CHECK_INT(g_req[1], MODBUS_FC_WRITE_SINGLE_REG);
    TF_CHECK_INT(g_req[4], 0x12);
    TF_CHECK_INT(g_req[5], 0x34);
    tf_end();

    tf_begin("modbus_master/写多线圈与多寄存器");
    bits[0] = 0xCD;
    bits[1] = 0x01;
    pdu[0] = MODBUS_FC_WRITE_MULTI_COILS;
    pdu[1] = 0x00;
    pdu[2] = 0x03;
    pdu[3] = 0x00;
    pdu[4] = 0x0A;
    set_response(MODBUS_RTU, 1, pdu, 5);
    TF_CHECK_INT(modbus_master_write_coils(&master, 3, 10, bits), 0);
    TF_CHECK_INT(g_req[5], 10);
    TF_CHECK_INT(g_req[6], 2);
    TF_CHECK_INT(g_req[7], 0xCD);
    TF_CHECK_INT(g_req[8], 0x01);

    pdu[4] = 9;                    /* 回显数量不符 */
    set_response(MODBUS_RTU, 1, pdu, 5);
    TF_CHECK_INT(modbus_master_write_coils(&master, 3, 10, bits),
                 MODBUS_ERR_FRAME);

    regs[0] = 0x1111;
    regs[1] = 0x2222;
    pdu[0] = MODBUS_FC_WRITE_MULTI_REGS;
    pdu[1] = 0x00;
    pdu[2] = 0x04;
    pdu[3] = 0x00;
    pdu[4] = 0x02;
    set_response(MODBUS_RTU, 1, pdu, 5);
    TF_CHECK_INT(modbus_master_write_regs(&master, 4, 2, regs), 0);
    TF_CHECK_INT(g_req[6], 4);
    TF_CHECK_INT(g_req[7], 0x11);
    TF_CHECK_INT(g_req[8], 0x11);
    TF_CHECK_INT(g_req[9], 0x22);
    TF_CHECK_INT(g_req[10], 0x22);
    tf_end();
}

static void test_errors_and_tcp(void)
{
    modbus_master_t master;
    uint16_t regs[1];
    uint8_t pdu[8];

    tf_begin("modbus_master/异常码、帧错误与传输失败");
    g_transport_fail = 0;
    TF_CHECK_INT(modbus_master_init(&master, MODBUS_RTU, 1,
                                    scripted_transport, NULL, 100), 0);
    pdu[0] = (uint8_t)(MODBUS_FC_READ_HOLDING_REGS | 0x80);
    pdu[1] = MODBUS_EX_ILLEGAL_DATA_ADDR;
    set_response(MODBUS_RTU, 1, pdu, 2);
    TF_CHECK_INT(modbus_master_read_holding(&master, 0, 1, regs),
                 MODBUS_EX_ILLEGAL_DATA_ADDR);
    TF_CHECK_INT(master.error_count, 1);

    pdu[0] = MODBUS_FC_READ_INPUT_REGS;
    pdu[1] = 2;
    pdu[2] = 0x00;
    pdu[3] = 0x01;
    set_response(MODBUS_RTU, 1, pdu, 4);
    TF_CHECK_INT(modbus_master_read_holding(&master, 0, 1, regs),
                 MODBUS_ERR_FRAME);
    TF_CHECK_INT(master.error_count, 2);

    g_transport_fail = 1;
    TF_CHECK_INT(modbus_master_read_holding(&master, 0, 1, regs),
                 MODBUS_ERR_TRANSPORT);
    TF_CHECK_INT(master.error_count, 3);
    g_transport_fail = 0;

    g_resp_len = 0;                /* 空响应 */
    TF_CHECK_INT(modbus_master_read_holding(&master, 0, 1, regs),
                 MODBUS_ERR_TRANSPORT);
    tf_end();

    tf_begin("modbus_master/TCP 事务号");
    g_resp_len = 0;
    TF_CHECK_INT(modbus_master_init(&master, MODBUS_TCP, 1,
                                    scripted_transport, NULL, 100), 0);
    pdu[0] = MODBUS_FC_READ_HOLDING_REGS;
    pdu[1] = 2;
    pdu[2] = 0x00;
    pdu[3] = 0x55;
    set_response(MODBUS_TCP, 1, pdu, 4);
    TF_CHECK_INT(modbus_master_read_holding(&master, 0, 1, regs), 0);
    TF_CHECK_INT(regs[0], 0x55);
    TF_CHECK_INT(master.transaction_id, 1);
    TF_CHECK_INT(g_req[0], 0x00);
    TF_CHECK_INT(g_req[1], 0x01);
    TF_CHECK_INT(g_req[5], 0x06);
    TF_CHECK_INT(g_req[6], 0x01);

    TF_CHECK_INT(modbus_master_read_holding(&master, 0, 1, regs), 0);
    TF_CHECK_INT(master.transaction_id, 2);
    TF_CHECK_INT(g_req[0], 0x00);
    TF_CHECK_INT(g_req[1], 0x02);
    tf_end();
}

void ut_modbus_master(void)
{
    test_init_params();
    test_reads();
    test_writes();
    test_errors_and_tcp();
}
