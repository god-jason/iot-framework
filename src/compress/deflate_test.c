/*
 * deflate_test.c - DEFLATE / INFLATE 单元测试
 *
 * 覆盖：
 *   - 自定义压缩（固定 Huffman / 存储块）与解压往返；
 *   - 压缩率与 deflate_bound 上界；
 *   - 动态 Huffman 块解码（内嵌 zlib 生成的 raw deflate 向量）；
 *   - 截断流、非法存储头、输出越界等错误路径。
 */

#include "test.h"
#include "deflate.h"

#include <string.h>

#define T_MAX      4096U
#define T_ENC_MAX  (T_MAX * 2U)

static uint8_t s_in[T_MAX];
static uint8_t s_enc[T_ENC_MAX];
static uint8_t s_dec[T_MAX];

/* 流式解压上下文（32KB 窗口）仅测试用，放静态区 */
static deflate_inflate_t s_stream;

/* 动态 Huffman 向量：zlib 对下方 gen_skew(300) 生成的 raw deflate */
static const uint8_t s_dyn[] = {
    0x1D, 0x8E, 0x89, 0x0D, 0xC3, 0x30, 0x10, 0xC3, 0x66, 0x95, 0xEE, 0xF5,
    0xED, 0x3F, 0x40, 0x99, 0x02, 0x81, 0xED, 0x08, 0x7A, 0x68, 0x45, 0x29,
    0x42, 0x5E, 0x35, 0x57, 0x6A, 0x56, 0xD2, 0x99, 0x43, 0x6B, 0x2B, 0x5D,
    0x0E, 0x3D, 0xD9, 0x0E, 0xBB, 0x33, 0x34, 0xA8, 0x17, 0x0F, 0xD5, 0xDA,
    0x68, 0x27, 0xDE, 0x71, 0xE9, 0x5D, 0x93, 0x20, 0xF6, 0x52, 0x8E, 0x9B,
    0x71, 0x54, 0x88, 0x20, 0x12, 0xDE, 0xE6, 0xAA, 0x12, 0xED, 0xA7, 0x32,
    0xF2, 0xA3, 0xF2, 0x45, 0xBF, 0xE6, 0x1D, 0xEA, 0xBC, 0x6F, 0x12, 0xB7,
    0x21, 0x1A, 0x15, 0xC6, 0xCD, 0x09, 0x06, 0x0B, 0x99, 0x56, 0xB3, 0xF5,
    0x75, 0x7C, 0x50, 0xF0, 0x51, 0x9F, 0xFC, 0x4E, 0x0A, 0x37, 0x8E, 0x80,
    0x9E, 0xA1, 0x05, 0x08, 0x61, 0x21, 0x2E, 0x3C, 0x4F, 0x1F, 0xF5, 0xC6,
    0xB5, 0x6B, 0x3E, 0x62, 0xBE, 0x6A, 0xDD, 0x82, 0x94, 0x3E, 0x62, 0xDF,
    0x66, 0x79, 0xC3, 0x7F, 0x8C, 0x73, 0xCE, 0xF9, 0x1B, 0xB2, 0xA9, 0xCF,
    0xE4, 0x84, 0xF4, 0x91, 0x52, 0xFF, 0x00
};

#define DYN_IN_LEN  300U

/* 与生成向量时相同的伪随机数据（偏斜字母表） */
static void gen_skew(uint8_t *out, uint32_t n)
{
    static const char alpha[] = "aaaaaaaabbbbbcccddefghij";
    const uint32_t alpha_n = (uint32_t)(sizeof(alpha) - 1U);
    uint32_t x = 12345U;
    uint32_t i;

    for (i = 0U; i < n; i++) {
        x = (x * 1103515245U + 12345U) & 0x7FFFFFFFU;
        out[i] = (uint8_t)alpha[(x >> 16) % alpha_n];
    }
}

static void roundtrip(const uint8_t *data, uint32_t len, uint32_t *enc_out)
{
    uint32_t enc = 0U;
    uint32_t dec = 0U;

    TF_CHECK_INT(deflate_compress(data, len, s_enc, sizeof(s_enc), &enc),
                 DEFLATE_OK);
    TF_CHECK(enc <= deflate_bound(len));
    TF_CHECK_INT(inflate_decompress(s_enc, enc, s_dec, sizeof(s_dec), &dec),
                 DEFLATE_OK);
    TF_CHECK_INT(dec, len);
    TF_CHECK_MEM(s_dec, data, len);
    if (enc_out != NULL) {
        *enc_out = enc;
    }
}

void ut_deflate(void)
{
    uint32_t i;
    uint32_t n;
    uint32_t enc;
    uint32_t dec;

    tf_begin("deflate/空与短输入");
    {
        TF_CHECK_INT(deflate_compress(s_in, 0U, s_enc, sizeof(s_enc), &enc),
                     DEFLATE_OK);
        TF_CHECK(enc <= deflate_bound(0U));
        TF_CHECK_INT(inflate_decompress(s_enc, enc, s_dec, sizeof(s_dec), &dec),
                     DEFLATE_OK);
        TF_CHECK_INT(dec, 0);

        memcpy(s_in, "hello deflate", 13U);
        roundtrip(s_in, 13U, NULL);
    }
    tf_end();

    tf_begin("deflate/重复与文本");
    {
        memset(s_in, 0x5A, 1024U);
        roundtrip(s_in, 1024U, &enc);
        TF_CHECK(enc < (1024U / 4U));    /* 重复数据应显著压缩 */

        n = 0U;
        while (n + 64U <= sizeof(s_in)) {
            memcpy(&s_in[n],
                   "the quick brown fox jumps over the lazy dog. 0123456789 ",
                   56U);
            n += 56U;
        }
        roundtrip(s_in, n, &enc);
        TF_CHECK(enc < (n / 2U));
    }
    tf_end();

    tf_begin("deflate/随机数据走存储块");
    {
        uint32_t x = 0x2468ACE0U;

        for (i = 0U; i < 1024U; i++) {
            x ^= x << 13;
            x ^= x >> 17;
            x ^= x << 5;
            s_in[i] = (uint8_t)x;
        }
        roundtrip(s_in, 1024U, &enc);
        /* 不可压缩时退化为存储块：n + 5 字节头 */
        TF_CHECK(enc <= (1024U + 5U));
        TF_CHECK(enc <= deflate_bound(1024U));
    }
    tf_end();

    tf_begin("deflate/动态 Huffman 解码");
    {
        gen_skew(s_dec, DYN_IN_LEN);
        TF_CHECK_INT(inflate_decompress(s_dyn, sizeof(s_dyn),
                                        s_in, sizeof(s_in), &dec),
                     DEFLATE_OK);
        TF_CHECK_INT(dec, DYN_IN_LEN);
        TF_CHECK_MEM(s_in, s_dec, DYN_IN_LEN);
    }
    tf_end();

    tf_begin("deflate/流式解压");
    {
        uint32_t chunk_len = 97U;
        uint32_t total = 0U;
        uint32_t produced = 0U;
        int done = 0;
        int rc;

        /* 自定义压缩 -> 小缓冲分批解压 */
        memset(s_in, 0x6C, 1000U);
        TF_CHECK_INT(deflate_compress(s_in, 1000U, s_enc, sizeof(s_enc), &enc),
                     DEFLATE_OK);
        TF_CHECK_INT(deflate_inflate_begin(&s_stream, s_enc, enc), DEFLATE_OK);
        total = 0U;
        while (done == 0) {
            rc = deflate_inflate_run(&s_stream, s_dec + total, chunk_len,
                                     &produced, &done);
            TF_CHECK_INT(rc, DEFLATE_OK);
            if (rc != DEFLATE_OK || produced == 0U) {
                break;
            }
            total += produced;
        }
        TF_CHECK_INT(done, 1);
        TF_CHECK_INT(total, 1000U);
        TF_CHECK_MEM(s_dec, s_in, 1000U);

        /* 动态 Huffman 向量 -> 17 字节一批，覆盖匹配跨批次 */
        TF_CHECK_INT(deflate_inflate_begin(&s_stream, s_dyn, sizeof(s_dyn)),
                     DEFLATE_OK);
        total = 0U;
        done = 0;
        while (done == 0) {
            rc = deflate_inflate_run(&s_stream, s_dec + total, 17U,
                                     &produced, &done);
            TF_CHECK_INT(rc, DEFLATE_OK);
            if (rc != DEFLATE_OK || produced == 0U) {
                break;
            }
            total += produced;
        }
        TF_CHECK_INT(done, 1);
        TF_CHECK_INT(total, DYN_IN_LEN);
        gen_skew(s_in, DYN_IN_LEN);
        TF_CHECK_MEM(s_dec, s_in, DYN_IN_LEN);

        /* 非法输入：截断的动态流 */
        TF_CHECK_INT(deflate_inflate_begin(&s_stream, s_dyn,
                                           sizeof(s_dyn) - 1U),
                     DEFLATE_OK);
        rc = DEFLATE_OK;
        done = 0;
        produced = 0U;
        while (rc == DEFLATE_OK && done == 0) {
            rc = deflate_inflate_run(&s_stream, s_dec, 17U, &produced, &done);
        }
        TF_CHECK_INT(rc, DEFLATE_ERR_DATA);
    }
    tf_end();

    tf_begin("deflate/错误路径");
    {
        /* 动态块截断 */
        TF_CHECK_INT(inflate_decompress(s_dyn, sizeof(s_dyn) - 1U,
                                        s_in, sizeof(s_in), &dec),
                     DEFLATE_ERR_DATA);

        /* 存储块 NLEN 校验失败：BFINAL=1 BTYPE=00，LEN=3，NLEN 错误 */
        s_enc[0] = 0x01U;
        s_enc[1] = 0x03U;
        s_enc[2] = 0x00U;
        s_enc[3] = 0x00U;
        s_enc[4] = 0x00U;
        s_enc[5] = 'a';
        s_enc[6] = 'b';
        s_enc[7] = 'c';
        TF_CHECK_INT(inflate_decompress(s_enc, 8U, s_dec, sizeof(s_dec), &dec),
                     DEFLATE_ERR_DATA);

        /* 输出缓冲不足 */
        memset(s_in, 0x11, 512U);
        TF_CHECK_INT(deflate_compress(s_in, 512U, s_enc, sizeof(s_enc), &enc),
                     DEFLATE_OK);
        TF_CHECK_INT(inflate_decompress(s_enc, enc, s_dec, 256U, &dec),
                     DEFLATE_ERR_SPACE);

        /* 目标缓冲过小 */
        TF_CHECK_INT(deflate_compress(s_in, 512U, s_enc, 4U, &enc),
                     DEFLATE_ERR_SPACE);

        TF_CHECK_INT(deflate_compress(NULL, 4U, s_enc, sizeof(s_enc), &enc),
                     DEFLATE_ERR_PARAM);
        TF_CHECK_INT(inflate_decompress(s_enc, 4U, s_dec, sizeof(s_dec), NULL),
                     DEFLATE_ERR_PARAM);
    }
    tf_end();
}
