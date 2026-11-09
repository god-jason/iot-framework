/*
 * crypto_test.c - 摘要与编码测试（crc / md5 / sha256 / base64）
 *
 * 使用公开标准测试向量验证正确性，并覆盖参数边界。
 */

#include "test.h"
#include "crc.h"
#include "md5.h"
#include "sha256.h"
#include "base64.h"

void ut_crc(void)
{
    static const char *s = "123456789";

    tf_begin("crc/标准向量");

    TF_CHECK_INT(crc8(s, 9U), 0xF4);
    TF_CHECK_INT(crc16_modbus(s, 9U), 0x4B37);
    TF_CHECK_INT(crc16_ccitt(s, 9U), 0x29B1);
    TF_CHECK_INT(crc32(s, 9U), 0xCBF43926UL);

    tf_end();

    tf_begin("crc/边界与自定义配置");

    TF_CHECK_INT(crc8(NULL, 0U), 0);
    TF_CHECK_INT(crc16_ccitt(NULL, 1U), 0);

    {
        crc_config_t cfg;
        cfg.polynomial = 0x1021U;
        cfg.initial = 0xFFFFU;
        cfg.xor_out = 0U;
        cfg.width = 16U;
        cfg.reflect_in = false;
        cfg.reflect_out = false;
        TF_CHECK_INT(crc_calculate(&cfg, s, 9U), 0x29B1);

        cfg.width = 0U;               /* 非法位宽 */
        TF_CHECK_INT(crc_calculate(&cfg, s, 9U), 0);
        TF_CHECK_INT(crc_calculate(NULL, s, 9U), 0);
    }

    tf_end();
}

void ut_md5(void)
{
    uint8_t digest[16];
    static const uint8_t expect_empty[16] = {
        0xD4, 0x1D, 0x8C, 0xD9, 0x8F, 0x00, 0xB2, 0x04,
        0xE9, 0x80, 0x09, 0x98, 0xEC, 0xF8, 0x42, 0x7E
    };
    static const uint8_t expect_abc[16] = {
        0x90, 0x01, 0x50, 0x98, 0x3C, 0xD2, 0x4F, 0xB0,
        0xD6, 0x96, 0x3F, 0x7D, 0x28, 0xE1, 0x7F, 0x72
    };
    md5_t ctx;

    tf_begin("md5/标准向量");

    md5_compute("", 0U, digest);
    TF_CHECK_MEM(digest, expect_empty, 16U);

    md5_compute("abc", 3U, digest);
    TF_CHECK_MEM(digest, expect_abc, 16U);

    tf_end();

    tf_begin("md5/分片与边界");

    md5_init(&ctx);
    md5_update(&ctx, "a", 1U);
    md5_update(&ctx, "bc", 2U);
    md5_update(&ctx, NULL, 5U);
    md5_update(&ctx, "abc", 0U);
    md5_final(&ctx, digest);
    TF_CHECK_MEM(digest, expect_abc, 16U);

    md5_init(NULL);
    md5_update(NULL, "a", 1U);
    md5_final(NULL, digest);
    md5_compute(NULL, 3U, digest);
    md5_compute("abc", 3U, NULL);
    TF_CHECK(1);

    tf_end();
}

void ut_sha256(void)
{
    uint8_t digest[32];
    static const uint8_t expect_abc[32] = {
        0xBA, 0x78, 0x16, 0xBF, 0x8F, 0x01, 0xCF, 0xEA,
        0x41, 0x41, 0x40, 0xDE, 0x5D, 0xAE, 0x22, 0x23,
        0xB0, 0x03, 0x61, 0xA3, 0x96, 0x17, 0x7A, 0x9C,
        0xB4, 0x10, 0xFF, 0x61, 0xF2, 0x00, 0x15, 0xAD
    };
    sha256_t ctx;

    tf_begin("sha256/标准向量");

    sha256_compute("abc", 3U, digest);
    TF_CHECK_MEM(digest, expect_abc, 32U);

    tf_end();

    tf_begin("sha256/分片与边界");

    sha256_init(&ctx);
    sha256_update(&ctx, "ab", 2U);
    sha256_update(&ctx, "c", 1U);
    sha256_final(&ctx, digest);
    TF_CHECK_MEM(digest, expect_abc, 32U);

    sha256_init(NULL);
    sha256_update(NULL, "a", 1U);
    sha256_final(NULL, digest);
    sha256_compute("abc", 3U, NULL);
    TF_CHECK(1);

    tf_end();
}

void ut_base64(void)
{
    char out[32];
    uint8_t bin[32];
    size_t written = 0;

    tf_begin("base64/编码");

    TF_CHECK_INT(base64_encode("abc", 3U, out, sizeof(out), &written), 0);
    TF_CHECK_STR(out, "YWJj");
    TF_CHECK_INT(written, 4);

    TF_CHECK_INT(base64_encode("ab", 2U, out, sizeof(out), NULL), 0);
    TF_CHECK_STR(out, "YWI=");

    TF_CHECK_INT(base64_encode("a", 1U, out, sizeof(out), NULL), 0);
    TF_CHECK_STR(out, "YQ==");

    TF_CHECK_INT(base64_encode("abc", 3U, out, 4U, &written), -1);
    TF_CHECK_INT(base64_encode(NULL, 1U, out, sizeof(out), &written), -1);

    tf_end();

    tf_begin("base64/解码与边界");

    TF_CHECK_INT(base64_decode("YWJj", 4U, bin, sizeof(bin), &written), 0);
    TF_CHECK_INT(written, 3);
    TF_CHECK_MEM(bin, "abc", 3U);

    TF_CHECK_INT(base64_decode("YWI=", 4U, bin, sizeof(bin), &written), 0);
    TF_CHECK_INT(written, 2);
    TF_CHECK_MEM(bin, "ab", 2U);

    TF_CHECK_INT(base64_decode("YWJj", 4U, bin, 2U, &written), -1);
    TF_CHECK_INT(base64_decode(NULL, 4U, bin, sizeof(bin), &written), -1);

    tf_end();
}
