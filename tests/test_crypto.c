/* 加密/校验模块测试：CRC、MD5、SHA-256。 */
#include "test_framework.h"
#include "crc.h"
#include "hash_md5.h"
#include "hash_sha256.h"

#include <stdint.h>

/* 标准校验值 "123456789" 的 CRC 结果 */
static void test_crc(void)
{
    const char *s = "123456789";

    tf_begin("crypto/crc 标准校验值");
    TF_CHECK_INT(crc8(s, 9), 0xF4u);        /* CRC-8/SMBUS */
    TF_CHECK_INT(crc16_modbus(s, 9), 0x4B37u);
    TF_CHECK_INT(crc16_ccitt(s, 9), 0x29B1u);
    TF_CHECK_INT(crc32(s, 9), 0xCBF43926u);
    tf_end();

    tf_begin("crypto/crc 通用接口");
    {
        crc_config_t cfg;
        cfg.polynomial = 0x1021u;
        cfg.initial = 0xFFFFu;
        cfg.xor_out = 0x0000u;
        cfg.width = 16;
        cfg.reflect_in = false;
        cfg.reflect_out = false;
        TF_CHECK_INT(crc_calculate(&cfg, s, 9), 0x29B1u);
    }
    TF_CHECK_INT(crc8("", 0), 0x00u);
    TF_CHECK_INT(crc_calculate(NULL, s, 9), 0u);
    tf_end();
}

static void test_md5(void)
{
    uint8_t digest[16];
    static const uint8_t expect_abc[16] = {
        0x90, 0x01, 0x50, 0x98, 0x3c, 0xd2, 0x4f, 0xb0,
        0xd6, 0x96, 0x3f, 0x7d, 0x28, 0xe1, 0x7f, 0x72
    };
    static const uint8_t expect_empty[16] = {
        0xd4, 0x1d, 0x8c, 0xd9, 0x8f, 0x00, 0xb2, 0x04,
        0xe9, 0x80, 0x09, 0x98, 0xec, 0xf8, 0x42, 0x7e
    };

    tf_begin("crypto/md5 已知摘要");
    hash_md5_compute("abc", 3, digest);
    TF_CHECK_MEM(digest, expect_abc, 16);

    hash_md5_compute("", 0, digest);
    TF_CHECK_MEM(digest, expect_empty, 16);
    tf_end();

    tf_begin("crypto/md5 分块更新");
    {
        hash_md5_t ctx;
        hash_md5_init(&ctx);
        hash_md5_update(&ctx, "a", 1);
        hash_md5_update(&ctx, "b", 1);
        hash_md5_update(&ctx, "c", 1);
        hash_md5_final(&ctx, digest);
        TF_CHECK_MEM(digest, expect_abc, 16);
    }
    tf_end();
}

static void test_sha256(void)
{
    uint8_t digest[32];
    static const uint8_t expect_abc[32] = {
        0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea,
        0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
        0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c,
        0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad
    };
    static const uint8_t expect_empty[32] = {
        0xe3, 0xb0, 0xc4, 0x42, 0x98, 0xfc, 0x1c, 0x14,
        0x9a, 0xfb, 0xf4, 0xc8, 0x99, 0x6f, 0xb9, 0x24,
        0x27, 0xae, 0x41, 0xe4, 0x64, 0x9b, 0x93, 0x4c,
        0xa4, 0x95, 0x99, 0x1b, 0x78, 0x52, 0xb8, 0x55
    };

    tf_begin("crypto/sha256 已知摘要");
    hash_sha256_compute("abc", 3, digest);
    TF_CHECK_MEM(digest, expect_abc, 32);

    hash_sha256_compute("", 0, digest);
    TF_CHECK_MEM(digest, expect_empty, 32);
    tf_end();

    tf_begin("crypto/sha256 分块更新");
    {
        hash_sha256_t ctx;
        hash_sha256_init(&ctx);
        hash_sha256_update(&ctx, "ab", 2);
        hash_sha256_update(&ctx, "c", 1);
        hash_sha256_final(&ctx, digest);
        TF_CHECK_MEM(digest, expect_abc, 32);
    }
    tf_end();
}

void test_crypto(void)
{
    test_crc();
    test_md5();
    test_sha256();
}
