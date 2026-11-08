/* core/hex.c 单元测试：十六进制编解码。 */
#include "test.h"
#include "hex.h"

#include <string.h>

void ut_hex(void)
{
    char out[64];
    uint8_t buf[32];
    size_t written;
    static const uint8_t bytes[] = {0x00, 0x01, 0xAB, 0xFF};
    static const uint8_t expect[] = {0x00, 0x01, 0xAB, 0xFF};

    tf_begin("hex/encode 基本");
    TF_CHECK_INT(hex_encode("Hello", 5, out, sizeof(out), true), 0);
    TF_CHECK_STR(out, "48656C6C6F");
    TF_CHECK_INT(hex_encode("Hello", 5, out, sizeof(out), false), 0);
    TF_CHECK_STR(out, "48656c6c6f");
    TF_CHECK_INT(hex_encode(bytes, 4, out, sizeof(out), true), 0);
    TF_CHECK_STR(out, "0001ABFF");
    tf_end();

    tf_begin("hex/encode 边界");
    TF_CHECK_INT(hex_encode("A", 1, out, 3, true), 0);
    TF_CHECK_STR(out, "41");
    /* 输出缓冲区刚好差 1 字节时应失败 */
    TF_CHECK_INT(hex_encode("A", 1, out, 2, true), -1);
    TF_CHECK_INT(hex_encode(NULL, 1, out, sizeof(out), true), -1);
    TF_CHECK_INT(hex_encode("A", 1, NULL, sizeof(out), true), -1);
    TF_CHECK_INT(hex_encode("", 0, out, 1, true), 0);
    TF_CHECK_STR(out, "");
    TF_CHECK_INT(hex_encode("", 0, out, 0, true), -1);
    tf_end();

    tf_begin("hex/decode 基本");
    memset(buf, 0, sizeof(buf));
    written = 0;
    TF_CHECK_INT(hex_decode("48656C6C6F", 10, buf, sizeof(buf), &written), 0);
    TF_CHECK_INT(written, 5);
    TF_CHECK_MEM(buf, "Hello", 5);
    TF_CHECK_INT(hex_decode("48656c6c6f", 10, buf, sizeof(buf), &written), 0);
    TF_CHECK_MEM(buf, "Hello", 5);
    TF_CHECK_INT(hex_decode("0001abff", 8, buf, sizeof(buf), NULL), 0);
    TF_CHECK_MEM(buf, expect, 4);
    tf_end();

    tf_begin("hex/decode 边界与错误");
    /* 奇数长度 */
    TF_CHECK_INT(hex_decode("ABC", 3, buf, sizeof(buf), &written), -1);
    /* 非法字符 */
    TF_CHECK_INT(hex_decode("GG", 2, buf, sizeof(buf), &written), -1);
    /* 输出缓冲区不足：3 字节需要 3，给 2 */
    TF_CHECK_INT(hex_decode("AABBCC", 6, buf, 2, &written), -1);
    /* 刚好够 */
    TF_CHECK_INT(hex_decode("AABB", 4, buf, 2, &written), 0);
    TF_CHECK_INT(written, 2);
    /* 空输入 */
    TF_CHECK_INT(hex_decode("", 0, buf, 0, &written), 0);
    TF_CHECK_INT(written, 0);
    /* 非法参数 */
    TF_CHECK_INT(hex_decode(NULL, 2, buf, sizeof(buf), &written), -1);
    TF_CHECK_INT(hex_decode("AA", 2, NULL, sizeof(buf), &written), -1);
    tf_end();
}
