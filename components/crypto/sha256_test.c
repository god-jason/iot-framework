/* crypto/hash/sha256.c per-file 单元测试。 */
#include "test.h"
#include "sha256.h"

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

void ut_sha256(void)
{
    uint8_t digest[32];
    char hex[65];
    sha256_t ctx;
    uint8_t chunk[1000];
    uint32_t i;

    tf_begin("sha256 FIPS 180-4 标准向量");
    sha256_compute("", 0, digest);
    digest_to_hex(digest, 32, hex);
    TF_CHECK_STR(hex,
                 "e3b0c44298fc1c149afbf4c8996fb924"
                 "27ae41e4649b934ca495991b7852b855");

    sha256_compute("abc", 3, digest);
    digest_to_hex(digest, 32, hex);
    TF_CHECK_STR(hex,
                 "ba7816bf8f01cfea414140de5dae2223"
                 "b00361a396177a9cb410ff61f20015ad");

    sha256_compute("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
                   56, digest);
    digest_to_hex(digest, 32, hex);
    TF_CHECK_STR(hex,
                 "248d6a61d20638b8e5c026930c3e6039"
                 "a33ce45964ff2167f6ecedd419db06c1");

    sha256_compute("abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmn"
                   "hijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu",
                   112, digest);
    digest_to_hex(digest, 32, hex);
    TF_CHECK_STR(hex,
                 "cf5b16a778af8380036ce59e7b049237"
                 "0b249b11e8f07a51afac45037afee9d1");
    tf_end();

    tf_begin("sha256 分块更新与一次性一致");
    {
        static const char *text =
            "The quick brown fox jumps over the lazy dog. "
            "0123456789 abcdefghijklmnopqrstuvwxyz ABCDEFGHIJKLMNOPQRSTUVWXYZ";
        uint32_t len = (uint32_t)strlen(text);
        static const uint32_t chunk_sizes[] = { 1, 3, 7, 55, 56, 63, 64, 65, 127 };
        uint32_t k;

        sha256_compute(text, len, digest);
        digest_to_hex(digest, 32, hex);

        for (k = 0; k < sizeof(chunk_sizes) / sizeof(chunk_sizes[0]); k++) {
            uint32_t off = 0;
            uint8_t d2[32];
            char h2[65];
            uint32_t step = chunk_sizes[k];

            sha256_init(&ctx);
            while (off < len) {
                uint32_t n = (len - off < step) ? (len - off) : step;
                sha256_update(&ctx, text + off, n);
                off += n;
            }
            sha256_final(&ctx, d2);
            digest_to_hex(d2, 32, h2);
            TF_CHECK_STR(h2, hex);
        }
    }
    tf_end();

    tf_begin("sha256 百万字节（跨多块）");
    memset(chunk, 'a', sizeof(chunk));
    sha256_init(&ctx);
    for (i = 0; i < 1000; i++) {
        sha256_update(&ctx, chunk, (uint32_t)sizeof(chunk));
    }
    sha256_final(&ctx, digest);
    digest_to_hex(digest, 32, hex);
    TF_CHECK_STR(hex,
                 "cdc76e5c9914fb9281a1c7e284d73e67"
                 "f1809a48a497200e046d39ccc7112cd0");
    tf_end();

    tf_begin("sha256 长度 0 与非法参数");
    sha256_init(NULL);
    sha256_init(&ctx);
    sha256_update(&ctx, NULL, 5);
    sha256_update(&ctx, "abc", 0);
    sha256_update(&ctx, NULL, 0);
    sha256_update(&ctx, "abc", 3);
    sha256_final(&ctx, digest);
    digest_to_hex(digest, 32, hex);
    TF_CHECK_STR(hex,
                 "ba7816bf8f01cfea414140de5dae2223"
                 "b00361a396177a9cb410ff61f20015ad");

    sha256_init(&ctx);
    sha256_update(&ctx, "abc", 3);
    sha256_final(&ctx, NULL);
    sha256_final(NULL, digest);

    sha256_compute(NULL, 0, digest);
    digest_to_hex(digest, 32, hex);
    TF_CHECK_STR(hex,
                 "e3b0c44298fc1c149afbf4c8996fb924"
                 "27ae41e4649b934ca495991b7852b855");
    tf_end();

    tf_begin("sha256 重复初始化状态复位");
    sha256_init(&ctx);
    sha256_update(&ctx, "garbage", 7);
    sha256_init(&ctx);
    sha256_update(&ctx, "abc", 3);
    sha256_final(&ctx, digest);
    digest_to_hex(digest, 32, hex);
    TF_CHECK_STR(hex,
                 "ba7816bf8f01cfea414140de5dae2223"
                 "b00361a396177a9cb410ff61f20015ad");

    TF_CHECK_INT(ctx.bit_count, 8u * 64u);
    tf_end();
}
