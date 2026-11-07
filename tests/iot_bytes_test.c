/* iot_bytes 单元测试：比较、相等、拷贝、填充、反转。 */
#include "test_framework.h"
#include "iot_bytes.h"

#include <stdint.h>

static void test_compare_equal(void)
{
    static const uint8_t a[] = {1, 2, 3, 4};
    static const uint8_t b[] = {1, 2, 3, 4};
    static const uint8_t less[] = {1, 2, 3, 0};
    static const uint8_t greater[] = {1, 2, 3, 5};

    tf_begin("bytes: compare 相等/大小关系");
    TF_CHECK_INT(iot_bytes_compare(a, b, sizeof(a)), 0);
    TF_CHECK_INT(iot_bytes_compare(a, less, sizeof(a)), 1);
    TF_CHECK_INT(iot_bytes_compare(a, greater, sizeof(a)), -1);
    TF_CHECK_INT(iot_bytes_compare(a, b, 0), 0);
    tf_end();

    tf_begin("bytes: equal");
    TF_CHECK(iot_bytes_equal(a, b, sizeof(a)) == true);
    TF_CHECK(iot_bytes_equal(a, less, sizeof(a)) == false);
    TF_CHECK(iot_bytes_equal(a, b, 0) == true);
    tf_end();

    tf_begin("bytes: compare 空指针");
    TF_CHECK_INT(iot_bytes_compare(NULL, NULL, 4), 0);
    TF_CHECK_INT(iot_bytes_compare(NULL, b, 4), -1);
    TF_CHECK_INT(iot_bytes_compare(a, NULL, 4), 1);
    TF_CHECK_INT(iot_bytes_compare(NULL, b, 0), 0);
    tf_end();
}

static void test_copy_fill(void)
{
    static const uint8_t src[] = {9, 8, 7, 6, 5};
    uint8_t buf[8];

    tf_begin("bytes: copy");
    memset(buf, 0xAA, sizeof(buf));
    iot_bytes_copy(buf, src, sizeof(src));
    TF_CHECK_MEM(buf, src, sizeof(src));
    TF_CHECK_INT(buf[5], 0xAA);
    iot_bytes_copy(buf, src, 0);
    TF_CHECK_INT(buf[5], 0xAA);
    tf_end();

    tf_begin("bytes: copy 非法参数不崩溃");
    iot_bytes_copy(NULL, src, 4);
    iot_bytes_copy(buf, NULL, 4);
    TF_CHECK(1);
    tf_end();

    tf_begin("bytes: fill");
    memset(buf, 0, sizeof(buf));
    iot_bytes_fill(buf, 0x5A, 3);
    TF_CHECK_INT(buf[0], 0x5A);
    TF_CHECK_INT(buf[1], 0x5A);
    TF_CHECK_INT(buf[2], 0x5A);
    TF_CHECK_INT(buf[3], 0);
    iot_bytes_fill(buf, 0xFF, sizeof(buf));
    TF_CHECK_INT(buf[7], 0xFF);
    iot_bytes_fill(buf, 0x00, 0);
    TF_CHECK_INT(buf[0], 0xFF);
    tf_end();

    tf_begin("bytes: fill 空指针/零长度");
    iot_bytes_fill(NULL, 0x11, 4);
    iot_bytes_fill(buf, 0x22, 0);
    TF_CHECK(1);
    tf_end();
}

static void test_reverse(void)
{
    uint8_t odd[] = {1, 2, 3, 4, 5};
    uint8_t even[] = {1, 2, 3, 4};
    uint8_t one[] = {0x42};
    uint8_t zero[] = {0x42, 0x43};
    static const uint8_t exp_odd[] = {5, 4, 3, 2, 1};
    static const uint8_t exp_even[] = {4, 3, 2, 1};

    tf_begin("bytes: reverse 奇偶长度");
    iot_bytes_reverse(odd, sizeof(odd));
    TF_CHECK_MEM(odd, exp_odd, sizeof(odd));
    iot_bytes_reverse(even, sizeof(even));
    TF_CHECK_MEM(even, exp_even, sizeof(even));
    iot_bytes_reverse(odd, sizeof(odd));
    TF_CHECK_MEM(odd, "\x01\x02\x03\x04\x05", sizeof(odd));
    tf_end();

    tf_begin("bytes: reverse 边界长度 0/1");
    iot_bytes_reverse(one, sizeof(one));
    TF_CHECK_INT(one[0], 0x42);
    iot_bytes_reverse(zero, 0);
    TF_CHECK_MEM(zero, "\x42\x43", 2);
    tf_end();

    tf_begin("bytes: reverse 空指针");
    iot_bytes_reverse(NULL, 4);
    TF_CHECK(1);
    tf_end();
}

void ut_iot_bytes(void)
{
    test_compare_equal();
    test_copy_fill();
    test_reverse();
}
