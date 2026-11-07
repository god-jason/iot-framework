/*
 * TLV（Tag-Length-Value）协议实现。
 *
 * 编码：tag(1) + length(2, 大端) + value(length)
 * 解码：严格校验剩余长度，任何截断或长度越界返回 -1。
 * 本模块不分配内存，所有结果均指向调用者提供的输入缓冲区。
 */
#include "tlv.h"

#include <string.h>

/* 读取大端 16 位长度 */
static uint16_t tlv_read_be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

/* 写出大端 16 位长度 */
static void tlv_write_be16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)(value >> 8);
    p[1] = (uint8_t)(value & 0xFFU);
}

int tlv_encode(uint8_t tag, const void *value, uint16_t length,
               uint8_t *out, size_t capacity, size_t *written)
{
    size_t total = TLV_HEADER_SIZE + (size_t)length;

    if (out == NULL || (value == NULL && length > 0U)) {
        return -1;
    }
    if (capacity < total) {
        return -1;
    }

    out[0] = tag;
    tlv_write_be16(&out[1], length);
    if (length > 0U) {
        memcpy(&out[TLV_HEADER_SIZE], value, length);
    }
    if (written != NULL) {
        *written = total;
    }
    return 0;
}

int tlv_encode_u8(uint8_t tag, uint8_t value, uint8_t *out,
                  size_t capacity, size_t *written)
{
    return tlv_encode(tag, &value, 1U, out, capacity, written);
}

int tlv_encode_u16(uint8_t tag, uint16_t value, uint8_t *out,
                   size_t capacity, size_t *written)
{
    uint8_t buf[2];

    tlv_write_be16(buf, value);
    return tlv_encode(tag, buf, 2U, out, capacity, written);
}

int tlv_encode_u32(uint8_t tag, uint32_t value, uint8_t *out,
                   size_t capacity, size_t *written)
{
    uint8_t buf[4];

    buf[0] = (uint8_t)(value >> 24);
    buf[1] = (uint8_t)(value >> 16);
    buf[2] = (uint8_t)(value >> 8);
    buf[3] = (uint8_t)value;
    return tlv_encode(tag, buf, 4U, out, capacity, written);
}

int tlv_decode(const uint8_t *data, size_t length,
               tlv_item_t *items, uint16_t max_items, uint16_t *count)
{
    tlv_cursor_t cursor;
    tlv_item_t item;
    uint16_t n = 0;
    int ret;

    if (data == NULL || count == NULL || (items == NULL && max_items > 0U)) {
        return -1;
    }

    tlv_cursor_init(&cursor, data, length);
    for (;;) {
        ret = tlv_cursor_next(&cursor, &item);
        if (ret < 0) {
            *count = n;
            return -1;
        }
        if (ret == 0) {
            break;
        }
        if (n >= max_items) {
            *count = n;
            return -2;
        }
        items[n++] = item;
    }

    *count = n;
    return 0;
}

const tlv_item_t *tlv_find(const tlv_item_t *items, uint16_t count, uint8_t tag)
{
    uint16_t i;

    if (items == NULL) {
        return NULL;
    }
    for (i = 0U; i < count; i++) {
        if (items[i].tag == tag) {
            return &items[i];
        }
    }
    return NULL;
}

void tlv_cursor_init(tlv_cursor_t *cursor, const uint8_t *data, size_t length)
{
    if (cursor == NULL) {
        return;
    }
    cursor->data = data;
    cursor->length = (data != NULL) ? length : 0U;
    cursor->offset = 0U;
}

int tlv_cursor_next(tlv_cursor_t *cursor, tlv_item_t *item)
{
    uint16_t value_len;

    if (cursor == NULL || item == NULL) {
        return -1;
    }
    if (cursor->data == NULL || cursor->offset >= cursor->length) {
        return 0;
    }
    if (cursor->length - cursor->offset < TLV_HEADER_SIZE) {
        return -1;      /* 头部不完整 */
    }

    item->tag = cursor->data[cursor->offset];
    value_len = tlv_read_be16(&cursor->data[cursor->offset + 1U]);
    cursor->offset += TLV_HEADER_SIZE;

    if (cursor->length - cursor->offset < value_len) {
        return -1;      /* 值区被截断 */
    }

    item->length = value_len;
    item->value = (value_len > 0U) ? &cursor->data[cursor->offset] : NULL;
    cursor->offset += value_len;
    return 1;
}

int tlv_read_u8(const tlv_item_t *item, uint8_t *value)
{
    if (item == NULL || value == NULL || item->length < 1U) {
        return -1;
    }
    *value = item->value[0];
    return 0;
}

int tlv_read_u16(const tlv_item_t *item, uint16_t *value)
{
    if (item == NULL || value == NULL || item->length < 2U) {
        return -1;
    }
    *value = tlv_read_be16(item->value);
    return 0;
}

int tlv_read_u32(const tlv_item_t *item, uint32_t *value)
{
    if (item == NULL || value == NULL || item->length < 4U) {
        return -1;
    }
    *value = ((uint32_t)item->value[0] << 24) |
             ((uint32_t)item->value[1] << 16) |
             ((uint32_t)item->value[2] << 8) |
             (uint32_t)item->value[3];
    return 0;
}
