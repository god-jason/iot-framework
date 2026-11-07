/* crypto/hash/md5.c per-file 单元测试。 */
#include "test_framework.h"
#include "hash_md5.h"

#include <string.h>

static void digest_to_hex(const uint8_t *digest, uint32_t len, char *out)
{
    static const char hex[] = "0123456789abcdef";
    uint32_t i;
    for (i = 0; i < len; i++) {
        out[i * 2] = hex[digest[i] >> 4];
        out[i * 2 + 1] = hex[digest[i] & 0x0F];
    }
    out[len * 2] = '\0';
}

void ut_md5(void)
{
    uint8_t digest[16];
    char hex[33];
    hash_md5_t ctx;
    uint8_t chunk[1000];
    uint32_t i;

    tf_begin("md5 RFC 1321 标准向量");
    hash_md5_compute("", 0, digest);
    digest_to_hex(digest, 16, hex);
    TF_CHECK_STR(hex, "d41d8cd98f00b204e9800998ecf8427e");

    hash_md5_compute("a", 1, digest);
    digest_to_hex(digest, 16, hex);
    TF_CHECK_STR(hex, "0cc175b9c0f1b6a831c399e269772661");

    hash_md5_compute("abc", 3, digest);
    digest_to_hex(digest, 16, hex);
    TF_CHECK_STR(hex, "900150983cd24fb0d6963f7d28e17f72");

    hash_md5_compute("message digest", 14, digest);
    digest_to_hex(digest, 16, hex);
    TF_CHECK_STR(hex, "f96b697d7cb7938d525a2f31aaf161d0");

    hash_md5_compute("abcdefghijklmnopqrstuvwxyz", 26, digest);
    digest_to_hex(digest, 16, hex);
    TF_CHECK_STR(hex, "c3fcd3d76192e4007dfb496cca67e13b");

    hash_md5_compute("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789",
                62, digest);
    digest_to_hex(digest, 16, hex);
    TF_CHECK_STR(hex, "d174ab98d277d9f5a5611c2c9f419d9f");

    hash_md5_compute("1234567890123456789012345678901234567890"
                "1234567890123456789012345678901234567890",
                80, digest);
    digest_to_hex(digest, 16, hex);
    TF_CHECK_STR(hex, "57edf4a22be3c955ac49da2e2107b67a");
    tf_end();

    tf_begin("md5 分组填充边界（56 字节）");
    hash_md5_compute("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
                56, digest);
    digest_to_hex(digest, 16, hex);
    TF_CHECK_STR(hex, "8215ef0796a20bcaaae116d3876c664a");
    tf_end();

    tf_begin("md5 分块更新与一次性一致");
    {
        static const char *text =
            "The quick brown fox jumps over the lazy dog. "
            "0123456789 abcdefghijklmnopqrstuvwxyz ABCDEFGHIJKLMNOPQRSTUVWXYZ";
        uint32_t len = (uint32_t)strlen(text);
        static const uint32_t chunk_sizes[] = { 1, 3, 7, 55, 56, 63, 64, 65, 127 };
        uint32_t k;

        hash_md5_compute(text, len, digest);
        digest_to_hex(digest, 16, hex);

        for (k = 0; k < sizeof(chunk_sizes) / sizeof(chunk_sizes[0]); k++) {
            uint32_t off = 0;
            uint8_t d2[16];
            char h2[33];
            uint32_t step = chunk_sizes[k];

            hash_md5_init(&ctx);
            while (off < len) {
                uint32_t n = (len - off < step) ? (len - off) : step;
                hash_md5_update(&ctx, text + off, n);
                off += n;
            }
            hash_md5_final(&ctx, d2);
            digest_to_hex(d2, 16, h2);
            TF_CHECK_STR(h2, hex);
        }
    }
    tf_end();

    tf_begin("md5 百万字节（跨多块）");
    memset(chunk, 'a', sizeof(chunk));
    hash_md5_init(&ctx);
    for (i = 0; i < 1000; i++) {
        hash_md5_update(&ctx, chunk, (uint32_t)sizeof(chunk));
    }
    hash_md5_final(&ctx, digest);
    digest_to_hex(digest, 16, hex);
    TF_CHECK_STR(hex, "7707d6ae4e027c70eea2a935c2296f21");
    tf_end();

    tf_begin("md5 长度 0 与非法参数");
    hash_md5_init(NULL);
    hash_md5_init(&ctx);
    hash_md5_update(&ctx, NULL, 5);
    hash_md5_update(&ctx, "abc", 0);
    hash_md5_update(&ctx, NULL, 0);
    hash_md5_update(&ctx, "abc", 3);
    hash_md5_final(&ctx, digest);
    digest_to_hex(digest, 16, hex);
    TF_CHECK_STR(hex, "900150983cd24fb0d6963f7d28e17f72");

    /* final 传 NULL 摘要不应崩溃 */
    hash_md5_init(&ctx);
    hash_md5_update(&ctx, "abc", 3);
    hash_md5_final(&ctx, NULL);
    hash_md5_final(NULL, digest);

    /* compute 传 NULL 数据按空串处理 */
    hash_md5_compute(NULL, 0, digest);
    digest_to_hex(digest, 16, hex);
    TF_CHECK_STR(hex, "d41d8cd98f00b204e9800998ecf8427e");
    tf_end();

    tf_begin("md5 重复初始化状态复位");
    hash_md5_init(&ctx);
    hash_md5_update(&ctx, "garbage", 7);
    hash_md5_init(&ctx);
    hash_md5_update(&ctx, "abc", 3);
    hash_md5_final(&ctx, digest);
    digest_to_hex(digest, 16, hex);
    TF_CHECK_STR(hex, "900150983cd24fb0d6963f7d28e17f72");

    TF_CHECK_INT(ctx.bit_count, 8u * 64u);   /* 3 + 填充共 64 字节 */
    tf_end();
}
