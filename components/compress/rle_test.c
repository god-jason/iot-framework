/*
 * rle_test.c - RLE 压缩/解压单元测试
 *
 * 覆盖：空输入、全重复、混合文本、随机数据、错误路径与上界。
 */

#include "test.h"
#include "rle.h"

#include <string.h>

#define T_MAX      2048U
#define T_ENC_MAX  (T_MAX + T_MAX / 128U + 8U)

static uint8_t s_in[T_MAX];
static uint8_t s_enc[T_ENC_MAX];
static uint8_t s_dec[T_MAX];

static void roundtrip(const uint8_t *data, uint32_t len, uint32_t *enc_out)
{
    uint32_t enc = 0U;
    uint32_t dec = 0U;

    TF_CHECK_INT(rle_encode(data, len, s_enc, sizeof(s_enc), &enc), RLE_OK);
    TF_CHECK(enc <= rle_bound(len));
    TF_CHECK_INT(rle_decode(s_enc, enc, s_dec, sizeof(s_dec), &dec), RLE_OK);
    TF_CHECK_INT(dec, len);
    TF_CHECK_MEM(s_dec, data, len);
    if (enc_out != NULL) {
        *enc_out = enc;
    }
}

void ut_rle(void)
{
    uint32_t i;
    uint32_t enc;
    uint32_t dec;
    uint32_t x;

    tf_begin("rle/空与短输入");
    {
        TF_CHECK_INT(rle_bound(0U), 0);
        TF_CHECK_INT(rle_encode(s_in, 0U, s_enc, sizeof(s_enc), &enc), RLE_OK);
        TF_CHECK_INT(enc, 0);
        TF_CHECK_INT(rle_decode(s_enc, 0U, s_dec, sizeof(s_dec), &dec), RLE_OK);
        TF_CHECK_INT(dec, 0);

        memcpy(s_in, "abc", 3U);
        roundtrip(s_in, 3U, NULL);
    }
    tf_end();

    tf_begin("rle/重复与混合数据");
    {
        memset(s_in, 0x77, T_MAX);
        roundtrip(s_in, T_MAX, &enc);
        TF_CHECK(enc < (T_MAX / 16U));      /* 纯重复压缩比很高 */

        /* 长短交替：例子混合字面量与重复组 */
        memset(s_in, 0, sizeof(s_in));
        for (i = 0U; i < 512U; i++) {
            s_in[i] = (uint8_t)(i % 7U);    /* 周期 7，无 3 连 */
        }
        s_in[100] = s_in[101] = s_in[102] = 0xEE;
        s_in[300] = s_in[301] = s_in[302] = s_in[303] = 0xEE;
        roundtrip(s_in, 512U, &enc);
    }
    tf_end();

    tf_begin("rle/随机数据");
    {
        x = 0x13579BDFU;
        for (i = 0U; i < 1024U; i++) {
            x ^= x << 13;
            x ^= x >> 17;
            x ^= x << 5;
            s_in[i] = (uint8_t)x;
        }
        roundtrip(s_in, 1024U, &enc);
        TF_CHECK(enc <= rle_bound(1024U));
    }
    tf_end();

    tf_begin("rle/错误路径");
    {
        /* 重复组缺少值 */
        s_enc[0] = 0x80U;                   /* count=1，重复组 */
        TF_CHECK_INT(rle_decode(s_enc, 1U, s_dec, sizeof(s_dec), &dec),
                     RLE_ERR_DATA);

        /* 字面量组被截断：声称 4 字节，只给 2 字节 */
        s_enc[0] = 0x03U;
        s_enc[1] = 'a';
        s_enc[2] = 'b';
        TF_CHECK_INT(rle_decode(s_enc, 3U, s_dec, sizeof(s_dec), &dec),
                     RLE_ERR_DATA);

        /* 输出缓冲不足 */
        memset(s_in, 0x22, 256U);
        TF_CHECK_INT(rle_encode(s_in, 256U, s_enc, sizeof(s_enc), &enc), RLE_OK);
        TF_CHECK_INT(rle_decode(s_enc, enc, s_dec, 128U, &dec),
                     RLE_ERR_SPACE);
        TF_CHECK_INT(rle_encode(s_in, 256U, s_enc, 8U, &enc), RLE_ERR_SPACE);
        TF_CHECK_INT(rle_encode(NULL, 4U, s_enc, sizeof(s_enc), &enc),
                     RLE_ERR_PARAM);
    }
    tf_end();
}
