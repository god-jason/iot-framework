/* modbus.c 单元测试：CRC、RTU/TCP 组帧与响应解析。 */
#include "test.h"
#include "modbus.h"

#include <string.h>

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
                 0);
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
                 0);
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
                                      0, 1, NULL, 0, adu, 8, &len), 0);
    TF_CHECK_INT(len, 8);
    TF_CHECK_INT(modbus_build_request(MODBUS_RTU, 1, MODBUS_FC_READ_COILS,
                                      0, 1, NULL, 0, adu, 7, &len), -1);
    TF_CHECK_INT(modbus_build_request(MODBUS_TCP, 1, MODBUS_FC_READ_COILS,
                                      0, 1, NULL, 0, adu, 12, &len), 0);
    TF_CHECK_INT(modbus_build_request(MODBUS_TCP, 1, MODBUS_FC_READ_COILS,
                                      0, 1, NULL, 0, adu, 11, &len), -1);
    tf_end();

    tf_begin("modbus/读请求数量边界");
    TF_CHECK_INT(modbus_build_request(MODBUS_RTU, 1, MODBUS_FC_READ_COILS,
                                      0, 2000, NULL, 0, adu, sizeof(adu), &len),
                 0);
    TF_CHECK_INT(modbus_build_request(MODBUS_RTU, 1, MODBUS_FC_READ_COILS,
                                      0, 2001, NULL, 0, adu, sizeof(adu), &len),
                 -1);
    TF_CHECK_INT(modbus_build_request(MODBUS_RTU, 1, MODBUS_FC_READ_HOLDING_REGS,
                                      0, 125, NULL, 0, adu, sizeof(adu), &len),
                 0);
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
                 0);
    TF_CHECK_INT(adu[3], 0x05);
    TF_CHECK_INT(adu[4], 0x00);
    TF_CHECK_INT(adu[5], 0x00);
    big[0] = 1;
    TF_CHECK_INT(modbus_build_request(MODBUS_RTU, 1, MODBUS_FC_WRITE_SINGLE_COIL,
                                      5, 1, big, 1, adu, sizeof(adu), &len),
                 0);
    TF_CHECK_INT(adu[3], 0x05);
    TF_CHECK_INT(adu[4], 0xFF);
    TF_CHECK_INT(adu[5], 0x00);

    big[0] = 0x12;
    big[1] = 0x34;
    TF_CHECK_INT(modbus_build_request(MODBUS_RTU, 1, MODBUS_FC_WRITE_SINGLE_REG,
                                      2, 1, big, 2, adu, sizeof(adu), &len),
                 0);
    TF_CHECK_INT(adu[2], 0x00);
    TF_CHECK_INT(adu[3], 0x02);
    TF_CHECK_INT(adu[4], 0x12);
    TF_CHECK_INT(adu[5], 0x34);
    TF_CHECK_INT(modbus_build_request(MODBUS_RTU, 1, MODBUS_FC_WRITE_SINGLE_REG,
                                      2, 1, big, 1, adu, sizeof(adu), &len),
                 0);
    TF_CHECK_INT(adu[4], 0x00);
    TF_CHECK_INT(adu[5], 0x00);
    tf_end();

    tf_begin("modbus/多点写请求与数量边界");
    memset(big, 0xCD, sizeof(big));
    TF_CHECK_INT(modbus_build_request(MODBUS_RTU, 1, MODBUS_FC_WRITE_MULTI_COILS,
                                      3, 10, big, 2, adu, sizeof(adu), &len),
                 0);
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
                 0);
    TF_CHECK_INT(modbus_build_request(MODBUS_RTU, 1, MODBUS_FC_WRITE_MULTI_COILS,
                                      3, 1969, big, 247, adu, sizeof(adu), &len),
                 -1);

    TF_CHECK_INT(modbus_build_request(MODBUS_RTU, 1, MODBUS_FC_WRITE_MULTI_REGS,
                                      0, 2, big, 4, adu, sizeof(adu), &len),
                 0);
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
                                       data, sizeof(data), &dlen), 0);
    TF_CHECK_INT(fc, 0x03);
    TF_CHECK_INT(dlen, 5);
    TF_CHECK_INT(data[0], 4);
    TF_CHECK_INT(data[1], 0x12);
    TF_CHECK_INT(data[4], 0xCD);
    tf_end();

    tf_begin("modbus/解析缓冲区截断与空输出");
    len = frame_rtu(adu, 1, pdu, 6);
    TF_CHECK_INT(modbus_parse_response(MODBUS_RTU, adu, len, NULL,
                                       data, 2, &dlen), 0);
    TF_CHECK_INT(dlen, 2);
    TF_CHECK_INT(modbus_parse_response(MODBUS_RTU, adu, len, &fc,
                                       NULL, 0, NULL), 0);
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
                                       data, sizeof(data), &dlen), 0);
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

void ut_modbus(void)
{
    test_crc_and_build();
    test_parse();
}
