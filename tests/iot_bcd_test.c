/* iot_bcd 单元测试：BCD 校验、编码（压缩 BCD，右对齐）与解码。 */
#include "test_framework.h"
#include "iot_bcd.h"

#include <stdint.h>

static void test_is_valid(void)
{
    static const uint8_t ok_data[] = {0x00, 0x12, 0x34, 0x99};
    static const uint8_t bad_low[] = {0x1A};
    static const uint8_t bad_high[] = {0xA1};
    static const uint8_t bad_mix[] = {0x12, 0xFF};

    tf_begin("bcd: is_valid 正常数据");
    TF_CHECK_INT(iot_bcd_is_valid(ok_data, sizeof(ok_data)), 0);
    TF_CHECK_INT(iot_bcd_is_valid(ok_data, 0), 0);
    tf_end();

    tf_begin("bcd: is_valid 非法半字节");
    TF_CHECK_INT(iot_bcd_is_valid(bad_low, 1), -1);
    TF_CHECK_INT(iot_bcd_is_valid(bad_high, 1), -1);
    TF_CHECK_INT(iot_bcd_is_valid(bad_mix, 2), -1);
    tf_end();

    tf_begin("bcd: is_valid 空指针");
    TF_CHECK_INT(iot_bcd_is_valid(NULL, 0), 0);
    TF_CHECK_INT(iot_bcd_is_valid(NULL, 1), -1);
    tf_end();
}

static void test_encode(void)
{
    uint8_t out[8];

    tf_begin("bcd: encode 常规值");
    memset(out, 0xEE, sizeof(out));
    TF_CHECK_INT(iot_bcd_encode(1234, out, 2), 0);
    TF_CHECK_INT(out[0], 0x12);
    TF_CHECK_INT(out[1], 0x34);

    memset(out, 0xEE, sizeof(out));
    TF_CHECK_INT(iot_bcd_encode(99, out, 1), 0);
    TF_CHECK_INT(out[0], 0x99);
    tf_end();

    tf_begin("bcd: encode 右对齐（位数不足补 0）");
    memset(out, 0xEE, sizeof(out));
    TF_CHECK_INT(iot_bcd_encode(12, out, 2), 0);
    TF_CHECK_INT(out[0], 0x00);
    TF_CHECK_INT(out[1], 0x12);

    memset(out, 0xEE, sizeof(out));
    TF_CHECK_INT(iot_bcd_encode(0, out, 2), 0);
    TF_CHECK_INT(out[0], 0x00);
    TF_CHECK_INT(out[1], 0x00);
    tf_end();

    tf_begin("bcd: encode 最大位数");
    memset(out, 0, sizeof(out));
    TF_CHECK_INT(iot_bcd_encode(1234567890u, out, 5), 0);
    TF_CHECK_INT(out[0], 0x12);
    TF_CHECK_INT(out[1], 0x34);
    TF_CHECK_INT(out[2], 0x56);
    TF_CHECK_INT(out[3], 0x78);
    TF_CHECK_INT(out[4], 0x90);

    memset(out, 0, sizeof(out));
    TF_CHECK_INT(iot_bcd_encode(4294967295u, out, 5), 0);
    TF_CHECK_INT(out[0], 0x42);
    TF_CHECK_INT(out[1], 0x94);
    TF_CHECK_INT(out[2], 0x96);
    TF_CHECK_INT(out[3], 0x72);
    TF_CHECK_INT(out[4], 0x95);
    tf_end();

    tf_begin("bcd: encode 缓冲区不足/非法参数");
    TF_CHECK_INT(iot_bcd_encode(100, out, 1), -1);   /* 3 位 > 2 半字节 */
    TF_CHECK_INT(iot_bcd_encode(12345, out, 2), -1); /* 5 位 > 4 半字节 */
    TF_CHECK_INT(iot_bcd_encode(1, NULL, 1), -1);
    TF_CHECK_INT(iot_bcd_encode(1, out, 0), -1);
    TF_CHECK_INT(iot_bcd_encode(99, out, 1), 0);
    tf_end();
}

static void test_decode(void)
{
    static const uint8_t d1[] = {0x12, 0x34};
    static const uint8_t d2[] = {0x00, 0x12};
    static const uint8_t d3[] = {0x99};
    static const uint8_t dmax[] = {0x42, 0x94, 0x96, 0x72, 0x95};
    static const uint8_t bad[] = {0x12, 0x3A};
    uint32_t value = 0xFFFFFFFFu;

    tf_begin("bcd: decode 常规值");
    value = 0;
    TF_CHECK_INT(iot_bcd_decode(d1, 2, &value), 0);
    TF_CHECK_INT(value, 1234);

    TF_CHECK_INT(iot_bcd_decode(d2, 2, &value), 0);
    TF_CHECK_INT(value, 12);

    TF_CHECK_INT(iot_bcd_decode(d3, 1, &value), 0);
    TF_CHECK_INT(value, 99);

    TF_CHECK_INT(iot_bcd_decode(dmax, 5, &value), 0);
    TF_CHECK_INT(value, 4294967295u);
    tf_end();

    tf_begin("bcd: decode 非法输入");
    value = 0;
    TF_CHECK_INT(iot_bcd_decode(bad, 2, &value), -1);
    TF_CHECK_INT(iot_bcd_decode(NULL, 2, &value), -1);
    TF_CHECK_INT(iot_bcd_decode(d1, 2, NULL), -1);
    tf_end();

    tf_begin("bcd: decode 零长度");
    value = 0xFFFFFFFFu;
    TF_CHECK_INT(iot_bcd_decode(d1, 0, &value), 0);
    TF_CHECK_INT(value, 0);
    tf_end();
}

static void test_roundtrip(void)
{
    static const uint32_t values[] = {0u, 1u, 12u, 99u, 1234u, 99999999u, 1234567890u};
    uint8_t buf[5];
    uint32_t decoded;
    size_t i;

    tf_begin("bcd: 编码解码往返");
    for (i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
        memset(buf, 0, sizeof(buf));
        TF_CHECK_INT(iot_bcd_encode(values[i], buf, 5), 0);
        decoded = 0;
        TF_CHECK_INT(iot_bcd_decode(buf, 5, &decoded), 0);
        TF_CHECK_INT(decoded, values[i]);
        TF_CHECK_INT(iot_bcd_is_valid(buf, 5), 0);
    }
    tf_end();
}

void ut_iot_bcd(void)
{
    test_is_valid();
    test_encode();
    test_decode();
    test_roundtrip();
}
