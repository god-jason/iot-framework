#include "iot_json.h"
#include "iot_string.h"

#include <stdio.h>

/* ---- 解析器 ---- */

static int json_alloc(iot_json_parser_t *p, iot_json_type_t type,
                      uint32_t start, uint32_t end, int32_t parent)
{
    iot_json_token_t *t;

    if (p->token_count >= p->token_capacity) {
        return IOT_ERR_OVERFLOW;
    }
    t = &p->tokens[p->token_count];
    t->type = type;
    t->start = start;
    t->end = end;
    t->parent = parent;
    t->child_count = 0;

    if (parent >= 0) {
        p->tokens[parent].child_count++;
    }
    return (int)p->token_count++;
}

static int json_skip_ws(const char *s, uint32_t len, uint32_t pos)
{
    while (pos < len && (s[pos] == ' ' || s[pos] == '\t' ||
                         s[pos] == '\r' || s[pos] == '\n')) {
        pos++;
    }
    return (int)pos;
}

/* 解析字符串（pos 指向开引号），返回闭引号后位置 */
static int json_parse_string(iot_json_parser_t *p, uint32_t pos, int32_t parent)
{
    uint32_t start = pos;
    pos++;   /* 跳过开引号 */
    while (pos < p->input_len) {
        char c = p->input[pos];
        if (c == '\\') {
            pos += 2;
            continue;
        }
        if (c == '"') {
            pos++;
            if (json_alloc(p, IOT_JSON_STRING, start, pos, parent) < 0) {
                return IOT_ERR_OVERFLOW;
            }
            return (int)pos;
        }
        pos++;
    }
    return IOT_ERR_INVALID;
}

static int json_ctype(const char *s, uint32_t start)
{
    char c = s[start];
    if (c == 't' || c == 'f') {
        return IOT_JSON_BOOL;
    }
    if (c == 'n') {
        return IOT_JSON_NULL;
    }
    return IOT_JSON_NUMBER;
}

/* 解析原始值 number/bool/null，返回结束位置 */
static int json_parse_primitive(iot_json_parser_t *p, uint32_t pos, int32_t parent)
{
    uint32_t start = pos;
    while (pos < p->input_len) {
        char c = p->input[pos];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' ||
            c == ',' || c == ']' || c == '}') {
            break;
        }
        pos++;
    }
    if (json_alloc(p, (iot_json_type_t)json_ctype(p->input, start),
                   start, pos, parent) < 0) {
        return IOT_ERR_OVERFLOW;
    }
    return (int)pos;
}

static int json_parse_value(iot_json_parser_t *p, uint32_t pos, int32_t parent);

/* 解析 array：pos 指向 '[' */
static int json_parse_array(iot_json_parser_t *p, uint32_t pos, int32_t parent)
{
    int arr;
    pos++;
    arr = json_alloc(p, IOT_JSON_ARRAY, pos - 1, 0, parent);
    if (arr < 0) {
        return arr;
    }

    pos = (uint32_t)json_skip_ws(p->input, p->input_len, pos);
    if (pos < p->input_len && p->input[pos] == ']') {
        p->tokens[arr].end = pos + 1;
        return (int)(pos + 1);
    }
    while (pos < p->input_len) {
        int r = json_parse_value(p, pos, arr);
        if (r < 0) {
            return r;
        }
        pos = (uint32_t)r;
        pos = (uint32_t)json_skip_ws(p->input, p->input_len, pos);
        if (pos < p->input_len && p->input[pos] == ',') {
            pos++;
            pos = (uint32_t)json_skip_ws(p->input, p->input_len, pos);
            continue;
        }
        break;
    }
    if (pos < p->input_len && p->input[pos] == ']') {
        pos++;
    } else {
        return IOT_ERR_INVALID;
    }
    p->tokens[arr].end = pos;
    return (int)pos;
}

/* 解析 object：pos 指向 '{' */
static int json_parse_object(iot_json_parser_t *p, uint32_t pos, int32_t parent)
{
    int obj;
    pos++;
    obj = json_alloc(p, IOT_JSON_OBJECT, pos - 1, 0, parent);
    if (obj < 0) {
        return obj;
    }

    pos = (uint32_t)json_skip_ws(p->input, p->input_len, pos);
    if (pos < p->input_len && p->input[pos] == '}') {
        p->tokens[obj].end = pos + 1;
        return (int)(pos + 1);
    }
    while (pos < p->input_len) {
        int r;
        if (p->input[pos] != '"') {
            return IOT_ERR_INVALID;
        }
        r = json_parse_string(p, pos, obj);   /* key */
        if (r < 0) {
            return r;
        }
        pos = (uint32_t)r;
        pos = (uint32_t)json_skip_ws(p->input, p->input_len, pos);
        if (pos >= p->input_len || p->input[pos] != ':') {
            return IOT_ERR_INVALID;
        }
        pos++;
        pos = (uint32_t)json_skip_ws(p->input, p->input_len, pos);
        r = json_parse_value(p, pos, obj);    /* value */
        if (r < 0) {
            return r;
        }
        pos = (uint32_t)r;
        pos = (uint32_t)json_skip_ws(p->input, p->input_len, pos);
        if (pos < p->input_len && p->input[pos] == ',') {
            pos++;
            pos = (uint32_t)json_skip_ws(p->input, p->input_len, pos);
            continue;
        }
        break;
    }
    if (pos < p->input_len && p->input[pos] == '}') {
        pos++;
    } else {
        return IOT_ERR_INVALID;
    }
    p->tokens[obj].end = pos;
    return (int)pos;
}

static int json_parse_value(iot_json_parser_t *p, uint32_t pos, int32_t parent)
{
    pos = (uint32_t)json_skip_ws(p->input, p->input_len, pos);
    if (pos >= p->input_len) {
        return IOT_ERR_INVALID;
    }
    switch (p->input[pos]) {
    case '{':
        return json_parse_object(p, pos, parent);
    case '[':
        return json_parse_array(p, pos, parent);
    case '"':
        return json_parse_string(p, pos, parent);
    default:
        return json_parse_primitive(p, pos, parent);
    }
}

int iot_json_parse(iot_json_parser_t *parser, const char *input, uint32_t len)
{
    int pos, r;

    if (parser == NULL || input == NULL) {
        return IOT_ERR_PARAM;
    }
    parser->input = input;
    parser->input_len = len;
    parser->token_count = 0;

    r = json_parse_value(parser, 0, -1);
    if (r < 0) {
        return r;
    }
    pos = json_skip_ws(input, len, (uint32_t)r);
    if (pos != (int)len) {
        return IOT_ERR_INVALID;   /* 多余内容 */
    }
    return IOT_OK;
}

/* ---- 访问函数 ---- */

static bool json_token_eq(const iot_json_parser_t *p, int32_t token, const char *key)
{
    const iot_json_token_t *t = &p->tokens[token];
    uint32_t i;
    uint32_t keylen = (uint32_t)iot_string_length(key);

    if (t->end - t->start - 2 != keylen) {
        return false;
    }
    for (i = 0; i < keylen; i++) {
        if (p->input[t->start + 1 + i] != key[i]) {
            return false;
        }
    }
    return true;
}

int iot_json_get_object(const iot_json_parser_t *parser, int32_t object,
                        const char *key, int32_t *value)
{
    uint32_t i;

    if (parser == NULL || key == NULL || value == NULL) {
        return IOT_ERR_PARAM;
    }
    for (i = 0; i < parser->token_count; i++) {
        const iot_json_token_t *t = &parser->tokens[i];
        if (t->parent == object && t->type == IOT_JSON_STRING) {
            if ((int32_t)(i + 1) < (int32_t)parser->token_count &&
                json_token_eq(parser, (int32_t)i, key)) {
                *value = (int32_t)(i + 1);
                return IOT_OK;
            }
        }
    }
    return IOT_ERR_NOT_FOUND;
}

int iot_json_get_array(const iot_json_parser_t *parser, int32_t array,
                       uint32_t index, int32_t *value)
{
    uint32_t i;
    uint32_t count = 0;

    if (parser == NULL || value == NULL) {
        return IOT_ERR_PARAM;
    }
    for (i = 0; i < parser->token_count; i++) {
        if (parser->tokens[i].parent == array) {
            if (count == index) {
                *value = (int32_t)i;
                return IOT_OK;
            }
            count++;
        }
    }
    return IOT_ERR_NOT_FOUND;
}

int iot_json_get_string(const iot_json_parser_t *parser, int32_t token,
                        const char **value, uint32_t *len)
{
    const iot_json_token_t *t;

    if (parser == NULL || value == NULL) {
        return IOT_ERR_PARAM;
    }
    t = &parser->tokens[token];
    if (t->type != IOT_JSON_STRING) {
        return IOT_ERR_INVALID;
    }
    *value = parser->input + t->start + 1;
    if (len != NULL) {
        *len = t->end - t->start - 2;
    }
    return IOT_OK;
}

int iot_json_get_int(const iot_json_parser_t *parser, int32_t token, int32_t *value)
{
    const iot_json_token_t *t;
    int32_t v = 0;
    int sign = 1;
    uint32_t i;

    if (parser == NULL || value == NULL) {
        return IOT_ERR_PARAM;
    }
    t = &parser->tokens[token];
    if (t->type != IOT_JSON_NUMBER) {
        return IOT_ERR_INVALID;
    }
    i = t->start;
    if (i < t->end && parser->input[i] == '-') {
        sign = -1;
        i++;
    }
    for (; i < t->end; i++) {
        char c = parser->input[i];
        if (c < '0' || c > '9') {
            break;
        }
        v = v * 10 + (c - '0');
    }
    *value = sign * v;
    return IOT_OK;
}

int iot_json_get_float(const iot_json_parser_t *parser, int32_t token, float *value)
{
    const iot_json_token_t *t;
    char buf[32];
    uint32_t len;

    if (parser == NULL || value == NULL) {
        return IOT_ERR_PARAM;
    }
    t = &parser->tokens[token];
    if (t->type != IOT_JSON_NUMBER) {
        return IOT_ERR_INVALID;
    }
    len = t->end - t->start;
    if (len >= sizeof(buf)) {
        return IOT_ERR_OVERFLOW;
    }
    iot_string_ncopy(buf, parser->input + t->start, len + 1);
    if (sscanf(buf, "%f", value) != 1) {
        return IOT_ERR_INVALID;
    }
    return IOT_OK;
}

int iot_json_get_bool(const iot_json_parser_t *parser, int32_t token, bool *value)
{
    const iot_json_token_t *t;

    if (parser == NULL || value == NULL) {
        return IOT_ERR_PARAM;
    }
    t = &parser->tokens[token];
    if (t->type != IOT_JSON_BOOL) {
        return IOT_ERR_INVALID;
    }
    *value = (parser->input[t->start] == 't');
    return IOT_OK;
}

/* ---- Writer ---- */

static int writer_put(iot_json_writer_t *w, const char *s, uint32_t len)
{
    if (w->length + len + 1 > w->capacity) {
        return IOT_ERR_OVERFLOW;
    }
    iot_string_ncopy(w->buffer + w->length, s, len + 1);
    w->length += len;
    w->buffer[w->length] = '\0';
    return IOT_OK;
}

static int writer_put_char(iot_json_writer_t *w, char c)
{
    char s[2];
    s[0] = c;
    s[1] = '\0';
    return writer_put(w, s, 1);
}

/* 写 value 前的逗号处理 */
static int writer_comma(iot_json_writer_t *w)
{
    if (w->depth > 0) {
        if (w->pending_key) {
            w->pending_key = false;
        } else {
            if (!w->first[w->depth]) {
                if (writer_put_char(w, ',') != IOT_OK) {
                    return IOT_ERR_OVERFLOW;
                }
            }
            w->first[w->depth] = false;
        }
    }
    return IOT_OK;
}

int iot_json_writer_init(iot_json_writer_t *writer, char *buffer, uint32_t capacity)
{
    if (writer == NULL || buffer == NULL || capacity == 0) {
        return IOT_ERR_PARAM;
    }
    writer->buffer = buffer;
    writer->capacity = capacity;
    writer->length = 0;
    writer->depth = 0;
    writer->pending_key = false;
    buffer[0] = '\0';
    return IOT_OK;
}

int iot_json_begin_object(iot_json_writer_t *writer)
{
    if (writer == NULL || writer->depth + 1 >= IOT_JSON_MAX_DEPTH) {
        return IOT_ERR_PARAM;
    }
    if (writer_comma(writer) != IOT_OK) {
        return IOT_ERR_OVERFLOW;
    }
    if (writer_put_char(writer, '{') != IOT_OK) {
        return IOT_ERR_OVERFLOW;
    }
    writer->depth++;
    writer->first[writer->depth] = true;
    return IOT_OK;
}

int iot_json_end_object(iot_json_writer_t *writer)
{
    if (writer == NULL || writer->depth == 0) {
        return IOT_ERR_STATE;
    }
    writer->depth--;
    return writer_put_char(writer, '}');
}

int iot_json_begin_array(iot_json_writer_t *writer)
{
    if (writer == NULL || writer->depth + 1 >= IOT_JSON_MAX_DEPTH) {
        return IOT_ERR_PARAM;
    }
    if (writer_comma(writer) != IOT_OK) {
        return IOT_ERR_OVERFLOW;
    }
    if (writer_put_char(writer, '[') != IOT_OK) {
        return IOT_ERR_OVERFLOW;
    }
    writer->depth++;
    writer->first[writer->depth] = true;
    return IOT_OK;
}

int iot_json_end_array(iot_json_writer_t *writer)
{
    if (writer == NULL || writer->depth == 0) {
        return IOT_ERR_STATE;
    }
    writer->depth--;
    return writer_put_char(writer, ']');
}

int iot_json_write_key(iot_json_writer_t *writer, const char *key)
{
    if (writer == NULL || key == NULL) {
        return IOT_ERR_PARAM;
    }
    if (writer->pending_key) {
        return IOT_ERR_STATE;   /* 上一 key 未写 value */
    }
    if (writer_comma(writer) != IOT_OK) {
        return IOT_ERR_OVERFLOW;
    }
    if (writer_put_char(writer, '"') != IOT_OK ||
        writer_put(writer, key, (uint32_t)iot_string_length(key)) != IOT_OK ||
        writer_put(writer, "\":", 2) != IOT_OK) {
        return IOT_ERR_OVERFLOW;
    }
    writer->pending_key = true;
    return IOT_OK;
}

int iot_json_write_string(iot_json_writer_t *writer, const char *value)
{
    if (writer == NULL || value == NULL) {
        return IOT_ERR_PARAM;
    }
    if (writer_comma(writer) != IOT_OK) {
        return IOT_ERR_OVERFLOW;
    }
    if (writer_put_char(writer, '"') != IOT_OK ||
        writer_put(writer, value, (uint32_t)iot_string_length(value)) != IOT_OK ||
        writer_put_char(writer, '"') != IOT_OK) {
        return IOT_ERR_OVERFLOW;
    }
    return IOT_OK;
}

int iot_json_write_number(iot_json_writer_t *writer, double value)
{
    char buf[32];

    if (writer == NULL) {
        return IOT_ERR_PARAM;
    }
    if (writer_comma(writer) != IOT_OK) {
        return IOT_ERR_OVERFLOW;
    }
    snprintf(buf, sizeof(buf), "%g", value);
    return writer_put(writer, buf, (uint32_t)iot_string_length(buf));
}

int iot_json_write_bool(iot_json_writer_t *writer, bool value)
{
    if (writer == NULL) {
        return IOT_ERR_PARAM;
    }
    if (writer_comma(writer) != IOT_OK) {
        return IOT_ERR_OVERFLOW;
    }
    return writer_put(writer, value ? "true" : "false",
                      value ? 4 : 5);
}

int iot_json_write_null(iot_json_writer_t *writer)
{
    if (writer == NULL) {
        return IOT_ERR_PARAM;
    }
    if (writer_comma(writer) != IOT_OK) {
        return IOT_ERR_OVERFLOW;
    }
    return writer_put(writer, "null", 4);
}

uint32_t iot_json_writer_length(const iot_json_writer_t *writer)
{
    return (writer == NULL) ? 0 : writer->length;
}