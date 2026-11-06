#ifndef IOT_JSON_H
#define IOT_JSON_H

/*
 * 轻量 JSON（tokenizer 风格）解析与生成。
 * 解析器：外部提供 token 数组，无动态内存，token 用字符串偏移定位，适合 F103。
 * Writer：外部提供输出缓冲，负责拼接，不申请内存。
 */

#include "iot_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    IOT_JSON_NULL,
    IOT_JSON_BOOL,
    IOT_JSON_NUMBER,
    IOT_JSON_STRING,
    IOT_JSON_ARRAY,
    IOT_JSON_OBJECT
} iot_json_type_t;

typedef struct {
    iot_json_type_t type;
    uint32_t start;        /* 值在 input 中的起始偏移 */
    uint32_t end;          /* 结束偏移（不含） */
    int32_t parent;        /* 父 token 索引，顶层为 -1 */
    int32_t child_count;   /* 直接子 token 数量（object 含 key+value） */
} iot_json_token_t;

typedef struct {
    const char *input;
    uint32_t input_len;

    iot_json_token_t *tokens;
    uint32_t token_capacity;
    uint32_t token_count;
} iot_json_parser_t;

int iot_json_parse(iot_json_parser_t *parser, const char *input, uint32_t len);

int iot_json_get_object(const iot_json_parser_t *parser, int32_t object,
                        const char *key, int32_t *value);
int iot_json_get_array(const iot_json_parser_t *parser, int32_t array,
                       uint32_t index, int32_t *value);
int iot_json_get_string(const iot_json_parser_t *parser, int32_t token,
                        const char **value, uint32_t *len);
int iot_json_get_int(const iot_json_parser_t *parser, int32_t token, int32_t *value);
int iot_json_get_float(const iot_json_parser_t *parser, int32_t token, float *value);
int iot_json_get_bool(const iot_json_parser_t *parser, int32_t token, bool *value);

/* ---- Writer ---- */
#define IOT_JSON_MAX_DEPTH 16

typedef struct {
    char *buffer;
    uint32_t capacity;
    uint32_t length;
    uint8_t depth;

    /* 内部字段 */
    bool first[IOT_JSON_MAX_DEPTH];
    bool pending_key;
} iot_json_writer_t;

int iot_json_writer_init(iot_json_writer_t *writer, char *buffer, uint32_t capacity);

int iot_json_begin_object(iot_json_writer_t *writer);
int iot_json_end_object(iot_json_writer_t *writer);
int iot_json_begin_array(iot_json_writer_t *writer);
int iot_json_end_array(iot_json_writer_t *writer);

int iot_json_write_key(iot_json_writer_t *writer, const char *key);
int iot_json_write_string(iot_json_writer_t *writer, const char *value);
int iot_json_write_number(iot_json_writer_t *writer, double value);
int iot_json_write_bool(iot_json_writer_t *writer, bool value);
int iot_json_write_null(iot_json_writer_t *writer);

uint32_t iot_json_writer_length(const iot_json_writer_t *writer);

#ifdef __cplusplus
}
#endif

#endif /* IOT_JSON_H */