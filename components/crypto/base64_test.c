/* base64 单元测试：编码（含填充）、解码、错误路径与往返。 */
#include "test.h"
#include "base64.h"

#include <stdint.h>

static void test_encode_known(void)
{
    char out[64];
    size_t written = 0;

    tf_begin("base64: encode 常规字符串");
    TF_CHECK_INT(base64_encode("Man", 3, out, sizeof(out), &written), 0);
    TF_CHECK_STR(out, "TWFu");
    TF_CHECK_INT(written, 4);

    TF_CHECK_INT(base64_encode("M", 1, out, sizeof(out), &written), 0);
    TF_CHECK_STR(out, "TQ==");
    TF_CHECK_INT(written, 4);

    TF_CHECK_INT(base64_encode("Ma", 2, out, sizeof(out), &written), 0);
    TF_CHECK_STR(out, "TWE=");
    TF_CHECK_INT(written, 4);

    TF_CHECK_INT(base64_encode("Hello", 5, out, sizeof(out), &written), 0);
    TF_CHECK_STR(out, "SGVsbG8=");
    TF_CHECK_INT(written, 8);
    tf_end();

    tf_begin("base64: encode 空输入");
    TF_CHECK_INT(base64_encode("", 0, out, sizeof(out), &written), 0);
    TF_CHECK_STR(out, "");
    TF_CHECK_INT(written, 0);
    tf_end();

    tf_begin("base64: encode 二进制数据");
    {
        static const uint8_t bin[] = {0x00, 0x01, 0x02, 0x03};
        TF_CHECK_INT(base64_encode(bin, sizeof(bin), out, sizeof(out), &written), 0);
        TF_CHECK_STR(out, "AAECAw==");
        TF_CHECK_INT(written, 8);
    }
    tf_end();
}

static void test_encode_errors(void)
{
    char out[8];
    size_t written = 123;

    tf_begin("base64: encode 缓冲区刚好够");
    TF_CHECK_INT(base64_encode("Man", 3, out, 5, &written), 0);
    TF_CHECK_STR(out, "TWFu");
    tf_end();

    tf_begin("base64: encode 缓冲区不足");
    written = 123;
    TF_CHECK_INT(base64_encode("Man", 3, out, 4, &written), -1);
    TF_CHECK_INT(base64_encode("M", 1, out, 4, &written), -1); /* 需要 5 字节 */
    TF_CHECK_INT(written, 123);                                     /* 失败时不写 written */
    tf_end();

    tf_begin("base64: encode 非法参数");
    TF_CHECK_INT(base64_encode(NULL, 3, out, sizeof(out), &written), -1);
    TF_CHECK_INT(base64_encode("Man", 3, NULL, sizeof(out), &written), -1);
    TF_CHECK(1);
    tf_end();

    tf_begin("base64: encode written 可为空");
    TF_CHECK_INT(base64_encode("Man", 3, out, sizeof(out), NULL), 0);
    TF_CHECK_STR(out, "TWFu");
    tf_end();
}

static void test_decode_known(void)
{
    uint8_t out[64];
    size_t written = 0;

    tf_begin("base64: decode 常规字符串");
    memset(out, 0, sizeof(out));
    TF_CHECK_INT(base64_decode("TWFu", 4, out, sizeof(out), &written), 0);
    TF_CHECK_INT(written, 3);
    TF_CHECK_MEM(out, "Man", 3);

    memset(out, 0, sizeof(out));
    TF_CHECK_INT(base64_decode("TQ==", 4, out, sizeof(out), &written), 0);
    TF_CHECK_INT(written, 1);
    TF_CHECK_MEM(out, "M", 1);

    memset(out, 0, sizeof(out));
    TF_CHECK_INT(base64_decode("TWE=", 4, out, sizeof(out), &written), 0);
    TF_CHECK_INT(written, 2);
    TF_CHECK_MEM(out, "Ma", 2);

    memset(out, 0, sizeof(out));
    TF_CHECK_INT(base64_decode("SGVsbG8sIFdvcmxkIQ==", 20, out, sizeof(out), &written),
                 0);
    TF_CHECK_INT(written, 13);
    TF_CHECK_MEM(out, "Hello, World!", 13);
    tf_end();

    tf_begin("base64: decode 忽略换行");
    memset(out, 0, sizeof(out));
    TF_CHECK_INT(base64_decode("TW\r\nFu", 6, out, sizeof(out), &written), 0);
    TF_CHECK_INT(written, 3);
    TF_CHECK_MEM(out, "Man", 3);
    tf_end();

    tf_begin("base64: decode 空输入与空 written");
    written = 99;
    TF_CHECK_INT(base64_decode("", 0, out, sizeof(out), &written), 0);
    TF_CHECK_INT(written, 0);
    TF_CHECK_INT(base64_decode("TWFu", 4, out, sizeof(out), NULL), 0);
    TF_CHECK_MEM(out, "Man", 3);
    tf_end();
}

static void test_decode_errors(void)
{
    uint8_t out[8];
    size_t written = 0;

    tf_begin("base64: decode 非法字符");
    TF_CHECK_INT(base64_decode("!!!!", 4, out, sizeof(out), &written), -1);
    TF_CHECK_INT(base64_decode("TW u", 4, out, sizeof(out), &written), -1);
    tf_end();

    tf_begin("base64: decode 输出缓冲区不足");
    TF_CHECK_INT(base64_decode("TWFu", 4, out, 2, &written), -1);
    TF_CHECK_INT(base64_decode("SGVsbG8=", 8, out, 4, &written), -1);
    TF_CHECK_INT(base64_decode("TWFu", 4, out, 3, &written), 0);
    tf_end();

    tf_begin("base64: decode 非法参数");
    TF_CHECK_INT(base64_decode(NULL, 4, out, sizeof(out), &written), -1);
    TF_CHECK_INT(base64_decode("TWFu", 4, NULL, sizeof(out), &written), -1);
    TF_CHECK(1);
    tf_end();
}

static void test_roundtrip(void)
{
    static const char *cases[] = {"", "a", "ab", "abc", "abcd", "Hello, World!"};
    char enc[64];
    uint8_t dec[64];
    size_t enc_len;
    size_t dec_len;
    size_t i;

    tf_begin("base64: 编码解码往返");
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        size_t len = (size_t)strlen(cases[i]);
        TF_CHECK_INT(base64_encode(cases[i], len, enc, sizeof(enc), &enc_len), 0);
        memset(dec, 0, sizeof(dec));
        TF_CHECK_INT(base64_decode(enc, enc_len, dec, sizeof(dec), &dec_len), 0);
        TF_CHECK_INT(dec_len, len);
        TF_CHECK_MEM(dec, cases[i], len);
    }
    tf_end();
}

void ut_base64(void)
{
    test_encode_known();
    test_encode_errors();
    test_decode_known();
    test_decode_errors();
    test_roundtrip();
}
