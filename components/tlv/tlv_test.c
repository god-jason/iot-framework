/* tlv.c 单元测试：编码、批量/游标解析、查找、整数辅助与错误路径。 */
#include "test.h"
#include "tlv.h"

#include <string.h>

void ut_tlv(void)
{
    uint8_t buf[64];
    size_t written = 0;
    uint16_t count = 0;
    tlv_item_t items[6];
    tlv_cursor_t cursor;
    tlv_item_t item;
    uint16_t off;
    uint8_t u8 = 0;
    uint16_t u16 = 0;
    uint32_t u32 = 0;

    tf_begin("tlv/编码基本帧");
    TF_CHECK_INT(tlv_encode(0x01U, "AB", 2U, buf, sizeof(buf), &written), 0);
    TF_CHECK_INT((int)written, 5);
    TF_CHECK_INT(buf[0], 0x01);
    TF_CHECK_INT(buf[1], 0x00);
    TF_CHECK_INT(buf[2], 0x02);
    TF_CHECK_INT(buf[3], 'A');
    TF_CHECK_INT(buf[4], 'B');

    TF_CHECK_INT(tlv_encode(0x02U, NULL, 0U, buf, sizeof(buf), &written), 0);
    TF_CHECK_INT((int)written, 3);
    TF_CHECK_INT(tlv_encode(0x02U, "A", 1U, buf, 3U, &written), -1);
    TF_CHECK_INT(tlv_encode(0x02U, NULL, 1U, buf, sizeof(buf), &written), -1);
    TF_CHECK_INT(tlv_encode(0x02U, "A", 1U, NULL, sizeof(buf), &written), -1);
    tf_end();

    tf_begin("tlv/整数编码");
    TF_CHECK_INT(tlv_encode_u8(0x10U, 0x5AU, buf, sizeof(buf), &written), 0);
    TF_CHECK_INT(buf[2], 1);
    TF_CHECK_INT(buf[3], 0x5A);
    TF_CHECK_INT(tlv_encode_u16(0x11U, 0x1234U, buf, sizeof(buf), &written), 0);
    TF_CHECK_INT(buf[2], 2);
    TF_CHECK_INT(buf[3], 0x12);
    TF_CHECK_INT(buf[4], 0x34);
    TF_CHECK_INT(tlv_encode_u32(0x12U, 0x01020304U, buf, sizeof(buf), &written), 0);
    TF_CHECK_INT(buf[2], 4);
    TF_CHECK_INT(buf[3], 0x01);
    TF_CHECK_INT(buf[6], 0x04);
    tf_end();

    tf_begin("tlv/批量解码与查找");
    off = 0;
    TF_CHECK_INT(tlv_encode_u8(0x20U, 7U, &buf[off], sizeof(buf) - off, &written), 0);
    off = (uint16_t)(off + written);
    TF_CHECK_INT(tlv_encode(0x21U, "xyz", 3U, &buf[off], sizeof(buf) - off, &written), 0);
    off = (uint16_t)(off + written);
    TF_CHECK_INT(tlv_encode(0x22U, NULL, 0U, &buf[off], sizeof(buf) - off, &written), 0);
    off = (uint16_t)(off + written);

    TF_CHECK_INT(tlv_decode(buf, off, items, 6U, &count), 0);
    TF_CHECK_INT(count, 3);
    TF_CHECK_INT(items[0].tag, 0x20);
    TF_CHECK_INT(items[0].length, 1);
    TF_CHECK_INT(items[1].tag, 0x21);
    TF_CHECK_INT(items[1].length, 3);
    TF_CHECK_INT(memcmp(items[1].value, "xyz", 3), 0);
    TF_CHECK_INT(items[2].tag, 0x22);
    TF_CHECK_INT(items[2].length, 0);
    TF_CHECK(items[2].value == NULL);

    TF_CHECK(tlv_find(items, count, 0x21U) == &items[1]);
    TF_CHECK(tlv_find(items, count, 0x33U) == NULL);
    TF_CHECK(tlv_find(NULL, count, 0x20U) == NULL);
    tf_end();

    tf_begin("tlv/解码错误与容量不足");
    buf[0] = 0x01U;
    buf[1] = 0x00U;
    TF_CHECK_INT(tlv_decode(buf, 2U, items, 6U, &count), -1);      /* 头不完整 */
    buf[2] = 0x05U;                                                /* 声明 5 字节 */
    TF_CHECK_INT(tlv_decode(buf, 4U, items, 6U, &count), -1);      /* 值被截断 */
    TF_CHECK_INT(tlv_decode(NULL, 4U, items, 6U, &count), -1);

    off = 0;
    TF_CHECK_INT(tlv_encode_u8(1U, 1U, &buf[off], sizeof(buf) - off, &written), 0);
    off = (uint16_t)(off + written);
    TF_CHECK_INT(tlv_encode_u8(2U, 2U, &buf[off], sizeof(buf) - off, &written), 0);
    off = (uint16_t)(off + written);
    TF_CHECK_INT(tlv_decode(buf, off, items, 1U, &count), -2);
    TF_CHECK_INT(count, 1);
    tf_end();

    tf_begin("tlv/游标解析");
    off = 0;
    TF_CHECK_INT(tlv_encode_u16(0x30U, 0xBEEFU, &buf[off], sizeof(buf) - off, &written), 0);
    off = (uint16_t)(off + written);
    TF_CHECK_INT(tlv_encode(0x31U, "ok", 2U, &buf[off], sizeof(buf) - off, &written), 0);
    off = (uint16_t)(off + written);

    tlv_cursor_init(&cursor, buf, off);
    TF_CHECK_INT(tlv_cursor_next(&cursor, &item), 1);
    TF_CHECK_INT(item.tag, 0x30);
    TF_CHECK_INT(tlv_read_u16(&item, &u16), 0);
    TF_CHECK_INT(u16, 0xBEEF);
    TF_CHECK_INT(tlv_cursor_next(&cursor, &item), 1);
    TF_CHECK_INT(item.tag, 0x31);
    TF_CHECK_INT(tlv_cursor_next(&cursor, &item), 0);

    tlv_cursor_init(&cursor, buf, 2U);
    TF_CHECK_INT(tlv_cursor_next(&cursor, &item), -1);
    tlv_cursor_init(&cursor, NULL, 10U);
    TF_CHECK_INT(tlv_cursor_next(&cursor, &item), 0);
    tf_end();

    tf_begin("tlv/整数读取");
    buf[0] = 1U;
    buf[1] = 0U;
    buf[2] = 4U;
    buf[3] = 0x11U;
    buf[4] = 0x22U;
    buf[5] = 0x33U;
    buf[6] = 0x44U;
    TF_CHECK_INT(tlv_decode(buf, 7U, items, 1U, &count), 0);
    TF_CHECK_INT(tlv_read_u8(&items[0], &u8), 0);
    TF_CHECK_INT(u8, 0x11);
    TF_CHECK_INT(tlv_read_u16(&items[0], &u16), 0);
    TF_CHECK_INT(u16, 0x1122);
    TF_CHECK_INT(tlv_read_u32(&items[0], &u32), 0);
    TF_CHECK_INT(u32, 0x11223344U);

    items[0].length = 1U;
    TF_CHECK_INT(tlv_read_u16(&items[0], &u16), -1);
    TF_CHECK_INT(tlv_read_u32(&items[0], &u32), -1);
    TF_CHECK_INT(tlv_read_u8(NULL, &u8), -1);
    TF_CHECK_INT(tlv_read_u8(&items[0], NULL), -1);
    tf_end();
}
