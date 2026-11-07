/* endian 单元测试：字节序转换与大小端读写。 */
#include "test.h"
#include "endian.h"

#include <stdint.h>

static void test_bswap(void)
{
    tf_begin("endian: bswap16/32/64");
    TF_CHECK_INT(bswap16(0x1234), 0x3412);
    TF_CHECK_INT(bswap16(0x0000), 0x0000);
    TF_CHECK_INT(bswap16(0xFFFF), 0xFFFF);
    TF_CHECK_INT(bswap16(bswap16(0xABCD)), 0xABCD);

    TF_CHECK_INT(bswap32(0x12345678u), 0x78563412u);
    TF_CHECK_INT(bswap32(0x00000000u), 0x00000000u);
    TF_CHECK_INT(bswap32(0xFFFFFFFFu), 0xFFFFFFFFu);
    TF_CHECK_INT(bswap32(bswap32(0xDEADBEEFu)), 0xDEADBEEFu);

    TF_CHECK(bswap64(0x0102030405060708ULL) == 0x0807060504030201ULL);
    TF_CHECK(bswap64(0x0000000000000000ULL) == 0x0000000000000000ULL);
    TF_CHECK(bswap64(0xFFFFFFFFFFFFFFFFULL) == 0xFFFFFFFFFFFFFFFFULL);
    TF_CHECK(bswap64(bswap64(0x0123456789ABCDEFULL)) == 0x0123456789ABCDEFULL);
    tf_end();
}

static void test_read_le(void)
{
    static const uint8_t le16[] = {0x34, 0x12};
    static const uint8_t le32[] = {0x78, 0x56, 0x34, 0x12};
    static const uint8_t le64[] = {0xEF, 0xCD, 0xAB, 0x89, 0x67, 0x45, 0x23, 0x01};

    tf_begin("endian: read_le16/32/64");
    TF_CHECK_INT(read_le16(le16), 0x1234);
    TF_CHECK_INT(read_le32(le32), 0x12345678u);
    TF_CHECK(read_le64(le64) == 0x0123456789ABCDEFULL);
    tf_end();

    tf_begin("endian: 小端读取非法参数返回 0");
    TF_CHECK_INT(read_le16(NULL), 0);
    TF_CHECK_INT(read_le32(NULL), 0);
    TF_CHECK(read_le64(NULL) == 0);
    tf_end();
}

static void test_read_be(void)
{
    static const uint8_t be16[] = {0x12, 0x34};
    static const uint8_t be32[] = {0x12, 0x34, 0x56, 0x78};
    static const uint8_t be64[] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF};

    tf_begin("endian: read_be16/32/64");
    TF_CHECK_INT(read_be16(be16), 0x1234);
    TF_CHECK_INT(read_be32(be32), 0x12345678u);
    TF_CHECK(read_be64(be64) == 0x0123456789ABCDEFULL);
    tf_end();

    tf_begin("endian: 大端读取非法参数返回 0");
    TF_CHECK_INT(read_be16(NULL), 0);
    TF_CHECK_INT(read_be32(NULL), 0);
    TF_CHECK(read_be64(NULL) == 0);
    tf_end();
}

static void test_write(void)
{
    static const uint8_t exp_le16[] = {0x34, 0x12};
    static const uint8_t exp_le32[] = {0x78, 0x56, 0x34, 0x12};
    static const uint8_t exp_le64[] = {0xEF, 0xCD, 0xAB, 0x89, 0x67, 0x45, 0x23, 0x01};
    static const uint8_t exp_be16[] = {0x12, 0x34};
    static const uint8_t exp_be32[] = {0x12, 0x34, 0x56, 0x78};
    static const uint8_t exp_be64[] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF};

    uint8_t buf[8];

    tf_begin("endian: write_le16/32/64");
    write_le16(buf, 0x1234);
    TF_CHECK_MEM(buf, exp_le16, 2);
    write_le32(buf, 0x12345678u);
    TF_CHECK_MEM(buf, exp_le32, 4);
    write_le64(buf, 0x0123456789ABCDEFULL);
    TF_CHECK_MEM(buf, exp_le64, 8);
    tf_end();

    tf_begin("endian: write_be16/32/64");
    write_be16(buf, 0x1234);
    TF_CHECK_MEM(buf, exp_be16, 2);
    write_be32(buf, 0x12345678u);
    TF_CHECK_MEM(buf, exp_be32, 4);
    write_be64(buf, 0x0123456789ABCDEFULL);
    TF_CHECK_MEM(buf, exp_be64, 8);
    tf_end();

    tf_begin("endian: 写入非法参数不崩溃");
    write_le16(NULL, 0x1234);
    write_le32(NULL, 0x12345678u);
    write_le64(NULL, 0x0123456789ABCDEFULL);
    write_be16(NULL, 0x1234);
    write_be32(NULL, 0x12345678u);
    write_be64(NULL, 0x0123456789ABCDEFULL);
    TF_CHECK(1);
    tf_end();
}

static void test_roundtrip(void)
{
    uint8_t buf[8];

    tf_begin("endian: 读写往返");
    write_le16(buf, 0xABCD);
    TF_CHECK_INT(read_le16(buf), 0xABCD);
    write_be16(buf, 0xABCD);
    TF_CHECK_INT(read_be16(buf), 0xABCD);

    write_le32(buf, 0x89ABCDEFu);
    TF_CHECK_INT(read_le32(buf), 0x89ABCDEFu);
    write_be32(buf, 0x89ABCDEFu);
    TF_CHECK_INT(read_be32(buf), 0x89ABCDEFu);

    write_le64(buf, 0x1122334455667788ULL);
    TF_CHECK(read_le64(buf) == 0x1122334455667788ULL);
    write_be64(buf, 0x1122334455667788ULL);
    TF_CHECK(read_be64(buf) == 0x1122334455667788ULL);
    tf_end();

    tf_begin("endian: 边界值全 0 / 全 1");
    write_le16(buf, 0);
    TF_CHECK_MEM(buf, "\x00\x00", 2);
    write_be16(buf, 0xFFFF);
    TF_CHECK_MEM(buf, "\xFF\xFF", 2);
    write_be32(buf, 0);
    TF_CHECK_INT(read_be32(buf), 0);
    write_le64(buf, 0xFFFFFFFFFFFFFFFFULL);
    TF_CHECK(read_le64(buf) == 0xFFFFFFFFFFFFFFFFULL);
    tf_end();
}

void ut_endian(void)
{
    test_bswap();
    test_read_le();
    test_read_be();
    test_write();
    test_roundtrip();
}
