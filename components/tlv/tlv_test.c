/*
 * tlv_test.c - TLV 编解码测试（对应 tlv.c）
 *
 * 覆盖：单项编码、定长整数编码、一次性解码、按 tag 查找、
 *       游标解析、帧不完整与容量不足。
 */

#include "test.h"
#include "tlv.h"

void ut_tlv(void)
{
    uint8_t buf[32];
    size_t written = 0;
    tlv_item_t items[4];
    uint16_t count = 0;
    size_t off = 0;

    tf_begin("tlv/编码");

    TF_CHECK_INT(tlv_encode(0x01U, "ab", 2U, buf, sizeof(buf), &written), 0);
    TF_CHECK_INT(written, 5);              /* tag + len(2) + value(2) */
    TF_CHECK_INT(buf[0], 0x01);
    TF_CHECK_INT(buf[1], 0x00);
    TF_CHECK_INT(buf[2], 0x02);
    TF_CHECK_INT(buf[3], 'a');
    TF_CHECK_INT(buf[4], 'b');

    /* 容量不足 */
    TF_CHECK_INT(tlv_encode(0x01U, "ab", 2U, buf, 4U, &written), -1);
    TF_CHECK_INT(tlv_encode(0x01U, NULL, 2U, buf, sizeof(buf), &written), -1);

    off = 5U;
    TF_CHECK_INT(tlv_encode_u8(0x02U, 0xABU, buf + off, sizeof(buf) - off,
                               &written), 0);
    TF_CHECK_INT(written, 4);
    off += written;
    TF_CHECK_INT(tlv_encode_u16(0x03U, 0x1234U, buf + off, sizeof(buf) - off,
                                &written), 0);
    TF_CHECK_INT(written, 5);
    off += written;
    TF_CHECK_INT(tlv_encode_u32(0x04U, 0x89ABCDEFUL, buf + off,
                                sizeof(buf) - off, &written), 0);
    TF_CHECK_INT(written, 7);
    off += written;

    tf_end();

    tf_begin("tlv/一次性解码与查找");

    TF_CHECK_INT(tlv_decode(buf, off, items, 4U, &count), 0);
    TF_CHECK_INT(count, 4);
    TF_CHECK_INT(items[0].tag, 0x01);
    TF_CHECK_INT(items[0].length, 2);
    TF_CHECK_MEM(items[0].value, "ab", 2U);

    {
        uint8_t v8 = 0U;
        uint16_t v16 = 0U;
        uint32_t v32 = 0U;
        const tlv_item_t *it;

        it = tlv_find(items, count, 0x02U);
        TF_CHECK(it != NULL);
        TF_CHECK_INT(tlv_read_u8(it, &v8), 0);
        TF_CHECK_INT(v8, 0xAB);

        it = tlv_find(items, count, 0x03U);
        TF_CHECK_INT(tlv_read_u16(it, &v16), 0);
        TF_CHECK_INT(v16, 0x1234);

        it = tlv_find(items, count, 0x04U);
        TF_CHECK_INT(tlv_read_u32(it, &v32), 0);
        TF_CHECK_INT(v32, 0x89ABCDEFUL);

        TF_CHECK(tlv_find(items, count, 0x09U) == NULL);

        /* 类型长度不足 */
        it = tlv_find(items, count, 0x02U);
        TF_CHECK_INT(tlv_read_u16(it, &v16), -1);
    }

    /* 结果表容量不足 */
    TF_CHECK_INT(tlv_decode(buf, off, items, 2U, &count), -2);

    tf_end();

    tf_begin("tlv/游标解析");

    {
        tlv_cursor_t cur;
        tlv_item_t item;
        int rc;
        int n = 0;

        tlv_cursor_init(&cur, buf, off);
        while ((rc = tlv_cursor_next(&cur, &item)) == 1) {
            n++;
        }
        TF_CHECK_INT(n, 4);
        TF_CHECK_INT(rc, 0);

        /* 帧不完整 */
        tlv_cursor_init(&cur, buf, 4U);
        TF_CHECK_INT(tlv_cursor_next(&cur, &item), -1);

        tlv_cursor_init(&cur, NULL, 0U);
        TF_CHECK_INT(tlv_cursor_next(&cur, &item), 0);
    }

    tf_end();

    tf_begin("tlv/非法参数");

    TF_CHECK_INT(tlv_decode(NULL, 4U, items, 4U, &count), -1);
    TF_CHECK_INT(tlv_decode(buf, off, NULL, 4U, &count), -1);
    TF_CHECK_INT(tlv_decode(buf, off, items, 4U, NULL), -1);
    TF_CHECK(tlv_find(NULL, 0U, 1U) == NULL);
    TF_CHECK_INT(tlv_read_u8(NULL, NULL), -1);

    tf_end();
}
