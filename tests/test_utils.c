/* 通用工具测试：Base64、BCD、HEX、字节/大小端、字符串。 */
#include "test_framework.h"
#include "iot_base64.h"
#include "iot_bcd.h"
#include "iot_hex.h"
#include "iot_bytes.h"
#include "iot_endian.h"
#include "iot_string.h"

#include <string.h>

static void test_base64(void)
{
    char out[64];
    uint8_t buf[64];
    iot_size_t written = 0;

    tf_begin("utils/base64 编码");
    TF_CHECK_INT(iot_base64_encode("Man", 3, out, sizeof(out), &written), IOT_OK);
    TF_CHECK_STR(out, "TWFu");
    TF_CHECK_INT(written, 4);

    iot_base64_encode("Ma", 2, out, sizeof(out), NULL);
    TF_CHECK_STR(out, "TWE=");
    iot_base64_encode("M", 1, out, sizeof(out), NULL);
    TF_CHECK_STR(out, "TQ==");
    iot_base64_encode("", 0, out, sizeof(out), NULL);
    TF_CHECK_STR(out, "");
    tf_end();

    tf_begin("utils/base64 解码");
    TF_CHECK_INT(iot_base64_decode("TWFu", 4, buf, sizeof(buf), &written), IOT_OK);
    TF_CHECK_INT(written, 3);
    TF_CHECK_MEM(buf, "Man", 3);

    TF_CHECK_INT(iot_base64_decode("TWE=", 4, buf, sizeof(buf), &written), IOT_OK);
    TF_CHECK_INT(written, 2);
    TF_CHECK_MEM(buf, "Ma", 2);

    TF_CHECK_INT(iot_base64_decode("!!!!", 4, buf, sizeof(buf), &written),
                 -1);
    tf_end();

    tf_begin("utils/base64 边界");
    TF_CHECK_INT(iot_base64_encode("abc", 3, out, 4, NULL), -1);
    TF_CHECK_INT(iot_base64_encode(NULL, 3, out, sizeof(out), NULL),
                 -1);
    TF_CHECK_INT(iot_base64_decode("TWFu", 4, buf, 2, &written),
                 -1);
    tf_end();
}

static void test_bcd(void)
{
    uint8_t buf[4];
    uint32_t value = 0;

    tf_begin("utils/bcd 编解码");
    memset(buf, 0, sizeof(buf));
    TF_CHECK_INT(iot_bcd_encode(1234, buf, 2), IOT_OK);
    TF_CHECK_INT(buf[0], 0x12);
    TF_CHECK_INT(buf[1], 0x34);

    memset(buf, 0, sizeof(buf));
    TF_CHECK_INT(iot_bcd_encode(7, buf, 1), IOT_OK);
    TF_CHECK_INT(buf[0], 0x07);
    tf_end();

    tf_begin("utils/bcd 解码与校验");
    {
        static const uint8_t good[2] = { 0x12, 0x34 };
        static const uint8_t bad[1] = { 0x1A };
        TF_CHECK_INT(iot_bcd_decode(good, 2, &value), IOT_OK);
        TF_CHECK_INT(value, 1234);
        TF_CHECK_INT(iot_bcd_decode(bad, 1, &value), -1);
        TF_CHECK_INT(iot_bcd_is_valid(good, 2), IOT_OK);
        TF_CHECK_INT(iot_bcd_is_valid(bad, 1), -1);
    }
    tf_end();

    tf_begin("utils/bcd 溢出");
    TF_CHECK_INT(iot_bcd_encode(123, buf, 1), -1);
    TF_CHECK_INT(iot_bcd_encode(1, NULL, 1), -1);
    tf_end();
}

static void test_hex(void)
{
    static const uint8_t raw[4] = { 0xDE, 0xAD, 0xBE, 0xEF };
    static const uint8_t de_ad[2] = { 0xDE, 0xAD };
    char out[16];
    uint8_t buf[8];
    iot_size_t written = 0;

    tf_begin("utils/hex 编码");
    TF_CHECK_INT(iot_hex_encode(raw, 4, out, sizeof(out), false), IOT_OK);
    TF_CHECK_STR(out, "deadbeef");
    iot_hex_encode(raw, 4, out, sizeof(out), true);
    TF_CHECK_STR(out, "DEADBEEF");
    tf_end();

    tf_begin("utils/hex 解码");
    TF_CHECK_INT(iot_hex_decode("deadbeef", 8, buf, sizeof(buf), &written),
                 IOT_OK);
    TF_CHECK_INT(written, 4);
    TF_CHECK_MEM(buf, raw, 4);

    TF_CHECK_INT(iot_hex_decode("DEAD", 4, buf, sizeof(buf), &written), IOT_OK);
    TF_CHECK_INT(written, 2);
    TF_CHECK_MEM(buf, de_ad, 2);
    tf_end();

    tf_begin("utils/hex 错误处理");
    TF_CHECK_INT(iot_hex_decode("abc", 3, buf, sizeof(buf), &written),
                 -1);
    TF_CHECK_INT(iot_hex_decode("zz", 2, buf, sizeof(buf), &written),
                 -1);
    TF_CHECK_INT(iot_hex_encode(raw, 4, out, 8, false), -1);
    tf_end();
}

static void test_bytes_endian(void)
{
    uint8_t a[4] = { 1, 2, 3, 4 };
    uint8_t b[4] = { 1, 2, 3, 4 };
    uint8_t c[4] = { 1, 2, 3, 5 };
    uint8_t buf[8];

    tf_begin("utils/bytes");
    TF_CHECK_INT(iot_bytes_compare(a, b, 4), 0);
    TF_CHECK(iot_bytes_equal(a, b, 4));
    TF_CHECK_INT(iot_bytes_compare(a, c, 4) < 0, 1);
    iot_bytes_reverse(a, 4);
    {
        static const uint8_t rev[4] = { 4, 3, 2, 1 };
        TF_CHECK_MEM(a, rev, 4);
    }
    iot_bytes_fill(buf, 0xAA, 4);
    TF_CHECK_INT(buf[0], 0xAA);
    TF_CHECK_INT(buf[3], 0xAA);
    tf_end();

    tf_begin("utils/endian 字节序转换");
    TF_CHECK_INT(iot_bswap16(0x1234), 0x3412);
    TF_CHECK_INT(iot_bswap32(0x11223344u), 0x44332211u);
    TF_CHECK(iot_bswap64(0x0102030405060708ULL) == 0x0807060504030201ULL);
    tf_end();

    tf_begin("utils/endian 读写");
    iot_write_be16(buf, 0x1234);
    TF_CHECK_INT(buf[0], 0x12);
    TF_CHECK_INT(iot_read_be16(buf), 0x1234);

    iot_write_le32(buf, 0x11223344u);
    TF_CHECK_INT(buf[0], 0x44);
    TF_CHECK_INT(iot_read_le32(buf), 0x11223344u);

    iot_write_be64(buf, 0x0102030405060708ULL);
    TF_CHECK(iot_read_be64(buf) == 0x0102030405060708ULL);

    iot_write_le64(buf, 0x0102030405060708ULL);
    TF_CHECK(iot_read_le64(buf) == 0x0102030405060708ULL);
    tf_end();
}

static void test_string(void)
{
    char buf[16];
    char split_buf[16];
    char *items[4];
    uint32_t count = 0;
    int32_t ival = 0;
    uint32_t uval = 0;
    float fval = 0.0f;

    tf_begin("utils/string 基础");
    TF_CHECK_INT(iot_string_length("hello"), 5);
    TF_CHECK_INT(iot_string_length(NULL), 0);
    iot_string_copy(buf, "hello");
    TF_CHECK_STR(buf, "hello");
    iot_string_ncopy(buf, "abcdef", 4);
    TF_CHECK_STR(buf, "abc");
    TF_CHECK_INT(iot_string_compare("abc", "abc"), 0);
    TF_CHECK(iot_string_compare("abc", "abd") < 0);
    TF_CHECK_INT(iot_string_ncompare("abcX", "abcY", 3), 0);
    tf_end();

    tf_begin("utils/string 查找与拆分");
    TF_CHECK(iot_string_find("hello world", "world") != NULL);
    TF_CHECK(iot_string_find("hello", "xyz") == NULL);
    TF_CHECK(iot_string_find_char("a,b", ',') != NULL);

    iot_string_copy(split_buf, "a,b,c");
    TF_CHECK_INT(iot_string_split(split_buf, ',', items, 4, &count), IOT_OK);
    TF_CHECK_INT(count, 3);
    TF_CHECK_STR(items[0], "a");
    TF_CHECK_STR(items[1], "b");
    TF_CHECK_STR(items[2], "c");
    tf_end();

    tf_begin("utils/string 数值转换");
    TF_CHECK_INT(iot_string_to_int("-123", &ival), IOT_OK);
    TF_CHECK_INT(ival, -123);
    TF_CHECK_INT(iot_string_to_uint("456", &uval), IOT_OK);
    TF_CHECK_INT(uval, 456);
    TF_CHECK_INT(iot_string_to_float("3.14", &fval), IOT_OK);
    TF_CHECK_NEAR(fval, 3.14, 0.001);
    TF_CHECK_INT(iot_string_to_int("abc", &ival), -1);
    tf_end();
}

void test_utils(void)
{
    test_base64();
    test_bcd();
    test_hex();
    test_bytes_endian();
    test_string();
}
