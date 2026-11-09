/*
 * bytes_test.c - 字节工具测试（bytes / bcd / endian / hex）
 *
 * 覆盖：比较/拷贝/填充/反转、BCD 编解码与合法性、
 *       大小端读写与字节序翻转、十六进制编解码边界。
 */

#include "test.h"
#include "bytes.h"
#include "bcd.h"
#include "endian.h"
#include "hex.h"

void ut_bytes(void)
{
    uint8_t a[4] = { 1, 2, 3, 4 };
    uint8_t b[4] = { 1, 2, 3, 4 };
    uint8_t c[4] = { 1, 2, 3, 5 };
    uint8_t dst[4];

    tf_begin("bytes/比较与判等");

    TF_CHECK_INT(bytes_compare(a, b, 4U), 0);
    TF_CHECK(bytes_compare(a, c, 4U) < 0);
    TF_CHECK(bytes_compare(c, a, 4U) > 0);
    TF_CHECK_INT(bytes_compare(a, b, 0U), 0);
    TF_CHECK_INT(bytes_compare(NULL, b, 4U), -1);
    TF_CHECK_INT(bytes_compare(a, NULL, 4U), 1);

    TF_CHECK(bytes_equal(a, b, 4U));
    TF_CHECK(!bytes_equal(a, c, 4U));
    TF_CHECK(!bytes_equal(NULL, b, 4U));
    TF_CHECK(bytes_equal(a, a, 4U));

    tf_end();

    tf_begin("bytes/拷贝、填充与反转");

    memset(dst, 0, sizeof(dst));
    bytes_copy(dst, a, sizeof(a));
    TF_CHECK_MEM(dst, a, sizeof(a));
    bytes_copy(NULL, a, 4U);          /* 安全空操作 */
    bytes_copy(dst, NULL, 4U);

    bytes_fill(dst, 0x5A, 4U);
    TF_CHECK_INT(dst[0], 0x5A);
    TF_CHECK_INT(dst[3], 0x5A);

    bytes_copy(dst, a, 4U);
    bytes_reverse(dst, 4U);
    TF_CHECK_INT(dst[0], 4);
    TF_CHECK_INT(dst[3], 1);
    bytes_reverse(dst, 1U);           /* len < 2 无操作 */
    TF_CHECK_INT(dst[0], 4);

    tf_end();
}

void ut_bcd(void)
{
    uint8_t buf[2];
    uint32_t value;

    tf_begin("bcd/编解码");

    TF_CHECK_INT(bcd_encode(12U, buf, 1U), 0);
    TF_CHECK_INT(buf[0], 0x12);

    TF_CHECK_INT(bcd_encode(1234U, buf, 2U), 0);
    TF_CHECK_INT(buf[0], 0x12);
    TF_CHECK_INT(buf[1], 0x34);

    TF_CHECK_INT(bcd_decode(buf, 2U, &value), 0);
    TF_CHECK_INT(value, 1234);

    /* 右对齐：1 位数字放在低半字节 */
    TF_CHECK_INT(bcd_encode(7U, buf, 1U), 0);
    TF_CHECK_INT(buf[0], 0x07);
    TF_CHECK_INT(bcd_decode(buf, 1U, &value), 0);
    TF_CHECK_INT(value, 7);

    tf_end();

    tf_begin("bcd/边界与非法值");

    TF_CHECK_INT(bcd_encode(1234U, buf, 1U), -1);    /* 容量不足 */
    TF_CHECK_INT(bcd_encode(1U, NULL, 1U), -1);
    TF_CHECK_INT(bcd_encode(1U, buf, 0U), -1);

    buf[0] = 0x1A;                                   /* 非法半字节 */
    TF_CHECK_INT(bcd_decode(buf, 1U, &value), -1);
    TF_CHECK_INT(bcd_is_valid(buf, 1U), -1);

    buf[0] = 0x99;
    TF_CHECK_INT(bcd_is_valid(buf, 1U), 0);
    TF_CHECK_INT(bcd_is_valid(NULL, 0U), 0);
    TF_CHECK_INT(bcd_is_valid(NULL, 1U), -1);
    TF_CHECK_INT(bcd_decode(NULL, 1U, &value), -1);
    TF_CHECK_INT(bcd_decode(buf, 1U, NULL), -1);

    tf_end();
}

void ut_endian(void)
{
    uint8_t buf[8];

    tf_begin("endian/翻转与读写");

    TF_CHECK_INT(bswap16(0x1234U), 0x3412);
    TF_CHECK_INT(bswap32(0x12345678UL), 0x78563412UL);
    TF_CHECK(bswap64(0x0102030405060708ULL) == 0x0807060504030201ULL);

    write_le16(buf, 0x1234U);
    TF_CHECK_INT(buf[0], 0x34);
    TF_CHECK_INT(buf[1], 0x12);
    TF_CHECK_INT(read_le16(buf), 0x1234);

    write_be16(buf, 0x1234U);
    TF_CHECK_INT(buf[0], 0x12);
    TF_CHECK_INT(buf[1], 0x34);
    TF_CHECK_INT(read_be16(buf), 0x1234);

    write_le32(buf, 0x12345678UL);
    TF_CHECK_INT(read_le32(buf), 0x12345678UL);
    write_be32(buf, 0x12345678UL);
    TF_CHECK_INT(read_be32(buf), 0x12345678UL);

    write_le64(buf, 0x0102030405060708ULL);
    TF_CHECK(read_le64(buf) == 0x0102030405060708ULL);
    write_be64(buf, 0x0102030405060708ULL);
    TF_CHECK(read_be64(buf) == 0x0102030405060708ULL);

    tf_end();

    tf_begin("endian/NULL 安全");

    TF_CHECK_INT(read_le16(NULL), 0);
    TF_CHECK_INT(read_be32(NULL), 0);
    TF_CHECK(read_le64(NULL) == 0U);
    write_le16(NULL, 1U);
    write_be32(NULL, 1U);
    write_le64(NULL, 1U);
    TF_CHECK(1);

    tf_end();
}

void ut_hex(void)
{
    char out[16];
    uint8_t bin[8];
    size_t written = 0;
    const uint8_t src[3] = { 0xDE, 0xAD, 0xBE };

    tf_begin("hex/编码");

    TF_CHECK_INT(hex_encode(src, 3U, out, sizeof(out), true), 0);
    TF_CHECK_STR(out, "DEADBE");
    TF_CHECK_INT(hex_encode(src, 3U, out, sizeof(out), false), 0);
    TF_CHECK_STR(out, "deadbe");

    /* 空间不足：需要 3*2+1 = 7 字节 */
    TF_CHECK_INT(hex_encode(src, 3U, out, 6U, true), -1);
    TF_CHECK_INT(hex_encode(NULL, 1U, out, sizeof(out), true), -1);
    TF_CHECK_INT(hex_encode(src, 0U, out, sizeof(out), true), 0);
    TF_CHECK_STR(out, "");

    tf_end();

    tf_begin("hex/解码");

    TF_CHECK_INT(hex_decode("DEADBE", 6U, bin, sizeof(bin), &written), 0);
    TF_CHECK_INT(written, 3);
    TF_CHECK_MEM(bin, src, 3U);

    TF_CHECK_INT(hex_decode("de", 2U, bin, sizeof(bin), NULL), 0);
    TF_CHECK_INT(bin[0], 0xDE);

    TF_CHECK_INT(hex_decode("ABC", 3U, bin, sizeof(bin), &written), -1);
    TF_CHECK_INT(hex_decode("ZZ", 2U, bin, sizeof(bin), &written), -1);
    TF_CHECK_INT(hex_decode("AABB", 4U, bin, 1U, &written), -1);
    TF_CHECK_INT(hex_decode(NULL, 2U, bin, sizeof(bin), &written), -1);

    tf_end();
}
