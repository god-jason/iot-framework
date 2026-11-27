/*
 * lzss_test.c - LZSS 压缩/解压单元测试
 *
 * 覆盖：空输入、短字面量、重复数据压缩率、文本/随机数据往返、
 *       输出空间不足、流截断与非法距离、重叠匹配、边界函数。
 */

#include "test.h"
#include "lzss.h"

#include <string.h>

#define T_MAX      2048U
#define T_ENC_MAX  (T_MAX + T_MAX / 8U + 8U)

static uint8_t s_in[T_MAX];
static uint8_t s_enc[T_ENC_MAX];
static uint8_t s_dec[T_MAX];

/* 可重复的伪随机序列（xorshift32） */
static uint32_t s_rnd = 0x12345678U;

static uint8_t rnd_byte(void)
{
    s_rnd ^= s_rnd << 13;
    s_rnd ^= s_rnd >> 17;
    s_rnd ^= s_rnd << 5;
    return (uint8_t)s_rnd;
}

/* 压缩 -> 解压并校验内容 */
static void roundtrip(const uint8_t *data, uint32_t len,
                      uint32_t *enc_len_out)
{
    uint32_t enc = 0U;
    uint32_t dec = 0U;

    TF_CHECK_INT(lzss_encode(data, len, s_enc, sizeof(s_enc), &enc), LZSS_OK);
    TF_CHECK(enc <= lzss_bound(len));
    TF_CHECK_INT(lzss_decode(s_enc, enc, s_dec, sizeof(s_dec), &dec), LZSS_OK);
    TF_CHECK_INT(dec, len);
    TF_CHECK_MEM(s_dec, data, len);
    if (enc_len_out != NULL) {
        *enc_len_out = enc;
    }
}

void ut_lzss(void)
{
    uint32_t i;
    uint32_t n;
    uint32_t enc;
    uint32_t dec;

    tf_begin("lzss/空与短输入");
    {
        TF_CHECK_INT(lzss_bound(0U), 0);
        TF_CHECK_INT(lzss_encode(s_in, 0U, s_enc, sizeof(s_enc), &enc),
                     LZSS_OK);
        TF_CHECK_INT(enc, 0);
        TF_CHECK_INT(lzss_decode(s_enc, 0U, s_dec, sizeof(s_dec), &dec),
                     LZSS_OK);
        TF_CHECK_INT(dec, 0);

        memcpy(s_in, "abc", 3U);
        roundtrip(s_in, 3U, NULL);
        memcpy(s_in, "hello world", 11U);
        roundtrip(s_in, 11U, NULL);
    }
    tf_end();

    tf_begin("lzss/重复数据");
    {
        memset(s_in, 0xAA, T_MAX);
        roundtrip(s_in, T_MAX, &enc);
        TF_CHECK(enc < (T_MAX / 4U));       /* 高度重复应显著压缩 */

        /* 周期串：验证重叠匹配（dist < len） */
        for (i = 0U; i < T_MAX; i++) {
            s_in[i] = (uint8_t)('a' + (i % 5U));
        }
        roundtrip(s_in, T_MAX, &enc);
        TF_CHECK(enc < (T_MAX / 2U));
    }
    tf_end();

    tf_begin("lzss/文本与随机数据");
    {
        static const char text[] =
            "robotos compress test: the quick brown fox jumps over the lazy "
            "dog. 0123456789 repeated text to exercise LZSS matches. ";

        n = 0U;
        while (n + sizeof(text) - 1U <= sizeof(s_in)) {
            memcpy(&s_in[n], text, sizeof(text) - 1U);
            n += (uint32_t)(sizeof(text) - 1U);
        }
        roundtrip(s_in, n, &enc);

        s_rnd = 0x12345678U;
        for (i = 0U; i < 1024U; i++) {
            s_in[i] = rnd_byte();
        }
        roundtrip(s_in, 1024U, &enc);
    }
    tf_end();

    tf_begin("lzss/错误路径");
    {
        memset(s_in, 0x5A, 256U);
        TF_CHECK_INT(lzss_encode(s_in, 256U, s_enc,
                                 lzss_bound(256U) - 1U, &enc),
                     LZSS_ERR_SPACE);

        TF_CHECK_INT(lzss_encode(NULL, 4U, s_enc, sizeof(s_enc), &enc),
                     LZSS_ERR_PARAM);

        /* 匹配对只剩 1 字节 -> 截断 */
        s_enc[0] = 0x00U;                   /* 一个匹配记号 */
        s_enc[1] = 0x00U;
        TF_CHECK_INT(lzss_decode(s_enc, 2U, s_dec, sizeof(s_dec), &dec),
                     LZSS_ERR_DATA);

        /* 距离大于已输出长度 -> 非法流 */
        s_enc[0] = 0x00U;
        s_enc[1] = 0xFFU;
        s_enc[2] = 0x0FU;                   /* dist = 4096 */
        TF_CHECK_INT(lzss_decode(s_enc, 3U, s_dec, sizeof(s_dec), &dec),
                     LZSS_ERR_DATA);

        /* 输出缓冲不足 */
        memset(s_in, 0x33, 512U);
        TF_CHECK_INT(lzss_encode(s_in, 512U, s_enc, sizeof(s_enc), &enc),
                     LZSS_OK);
        TF_CHECK_INT(lzss_decode(s_enc, enc, s_dec, 511U, &dec),
                     LZSS_ERR_SPACE);
    }
    tf_end();
}
