#ifndef TLV_H
#define TLV_H

/*
 * TLV（Tag-Length-Value）协议编解码。
 *
 * 帧格式：
 *   [tag:1][length:2 大端][value:length]
 *
 * 特点：
 *   - 纯 C99，无动态内存；输出缓冲区与解析结果均由调用者提供；
 *   - 提供一次性编码/解码、游标式流式解析与定长整数辅助接口；
 *   - 所有多字节长度与大端整数均按网络字节序处理。
 *
 * 错误码约定：0 成功；-1 参数非法或帧不完整；-2 结果表容量不足。
 */

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TLV_HEADER_SIZE 3U   /* tag(1) + length(2) */
#define TLV_MAX_LENGTH  0xFFFFU

/* 解码得到的一个 TLV 项；value 指向输入缓冲区内部，不拷贝 */
typedef struct {
    uint8_t tag;            /* 标签 */
    uint16_t length;        /* 值字节数 */
    const uint8_t *value;   /* 值首地址（length 为 0 时可为 NULL） */
} tlv_item_t;

/* 游标式解析器，适合逐项处理而不需要结果数组 */
typedef struct {
    const uint8_t *data;    /* 输入缓冲区 */
    size_t length;          /* 输入总长度 */
    size_t offset;          /* 当前解析偏移 */
} tlv_cursor_t;

/*
 * 编码一个 TLV 到 out。
 * 参数：tag - 标签；value - 值（length>0 时不可为 NULL）；length - 值长度；
 *       out/capacity - 输出缓冲与容量；written - 可选，返回写入字节数。
 * 返回：0 成功；-1 参数非法或容量不足。
 * 注意：本函数只写入一项，不做尾部追加。
 */
int tlv_encode(uint8_t tag, const void *value, uint16_t length,
               uint8_t *out, size_t capacity, size_t *written);

/* 编码定长无符号整数（大端存储为值字段） */
int tlv_encode_u8(uint8_t tag, uint8_t value, uint8_t *out,
                  size_t capacity, size_t *written);
int tlv_encode_u16(uint8_t tag, uint16_t value, uint8_t *out,
                   size_t capacity, size_t *written);
int tlv_encode_u32(uint8_t tag, uint32_t value, uint8_t *out,
                   size_t capacity, size_t *written);

/*
 * 一次性解析整个缓冲区。
 * 参数：data/length - 输入；items - 结果数组；max_items - 数组容量；
 *       count - 返回实际解析到的项数（失败时可能已写入部分）。
 * 返回：0 成功；-1 帧不完整/参数非法；-2 项数超过 max_items。
 * 注意：items 中的 value 指针指向 data 内部，data 必须保持有效。
 */
int tlv_decode(const uint8_t *data, size_t length,
               tlv_item_t *items, uint16_t max_items, uint16_t *count);

/* 在解码结果中按 tag 查找，找到返回值项指针，否则 NULL */
const tlv_item_t *tlv_find(const tlv_item_t *items, uint16_t count, uint8_t tag);

/* 初始化游标；data 为 NULL 时视为空帧 */
void tlv_cursor_init(tlv_cursor_t *cursor, const uint8_t *data, size_t length);

/*
 * 取下一项。
 * 返回：1 成功；0 已到末尾；-1 帧不完整。
 */
int tlv_cursor_next(tlv_cursor_t *cursor, tlv_item_t *item);

/* 从值项读取定长整数（要求 item->length >= 目标字节数，按大端解析） */
int tlv_read_u8(const tlv_item_t *item, uint8_t *value);
int tlv_read_u16(const tlv_item_t *item, uint16_t *value);
int tlv_read_u32(const tlv_item_t *item, uint32_t *value);

#ifdef __cplusplus
}
#endif

#endif /* TLV_H */
