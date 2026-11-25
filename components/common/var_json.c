/*
 * var_json.c - var_t 与 JSON 文本互转（自带精简解析器/打印器，不依赖 cJSON）
 *
 * 序列化：递归把 var 写成 JSON 文本；ARRAY/OBJECT 递归深度上限
 *         VAR_JSON_MAX_DEPTH，超过则失败，避免环形结构导致无限递归。
 * 反序列化：递归下降解析标量 JSON（null/bool/number/string），
 *           要求整个输入只有一个值（可带首尾空白）；数组/对象不支持。
 *
 * 内存：输出文本由 ros_mem_malloc 分配，调用者用 var_free_json() 释放；
 *       输入字符串解析到调用者提供的 strbuf，不额外分配。
 *
 * 结构：
 *   序列化   json_out_t + out_* 系列（按需扩容写入）
 *            + var_write / array_write / object_write
 *   反序列化 json_in_t + parse_* 系列（自左向右单遍扫描）
 *
 * 约定：
 *   - ARRAY 的 vec 元素必须是 var_t；OBJECT 的 map value 必须是 var_t*；
 *   - 字符串为借用语义：序列化不拷贝，反序列化写入 strbuf；
 *   - 不支持数组/对象的反序列化（on-device 只解析标量即可）；
 *   - 数字格式化/解析使用 strings.c 的 str_printf / str_parse_float，
 *     float 输出按 7 位有效数字自行拼装，不引入 libc 的浮点 printf/strtod。
 */

#include "var_json.h"

#include "ros_mem.h"
#include "strings.h"

#include <math.h>

/* 嵌套深度上限（防止环形引用） */
#define VAR_JSON_MAX_DEPTH      8U

/* ============================== 序列化 ============================== */

/*
 * 输出缓冲区：buf 由 ros_mem 管理，按需倍增扩容。
 * len 为已写入字节数（不含结尾 '\0'），cap 为 buf 容量。
 */
typedef struct {
    char    *buf;
    uint32_t len;
    uint32_t cap;
} json_out_t;

/*
 * 确保在已写入内容之外还能再追加 extra 字节（另加结尾 '\0'）。
 * 容量不足时从 64 字节起按 2 倍增长；成功返回 true，内存不足返回 false。
 */
static bool out_reserve(json_out_t *o, uint32_t extra)
{
    uint32_t need = o->len + extra + 1U;    /* +1 留给结尾 '\0' */
    uint32_t cap;
    char *p;

    if (need <= o->cap) {
        return true;
    }
    cap = (o->cap == 0U) ? 64U : o->cap;
    while (cap < need) {
        if (cap > 0x7FFFFFFFU) {
            return false;
        }
        cap *= 2U;
    }
    p = (char *)ros_mem_realloc(o->buf, cap);
    if (p == NULL) {
        return false;
    }
    o->buf = p;
    o->cap = cap;
    return true;
}

/* 追加 n 字节原始数据（不做任何转义），并维护结尾 '\0' */
static bool out_put(json_out_t *o, const char *s, uint32_t n)
{
    if (!out_reserve(o, n)) {
        return false;
    }
    memcpy(o->buf + o->len, s, n);
    o->len += n;
    o->buf[o->len] = '\0';
    return true;
}

/* 追加单个字符 */
static bool out_putc(json_out_t *o, char c)
{
    return out_put(o, &c, 1U);
}

/* 追加以 '\0' 结尾的字符串（strlen 计长） */
static bool out_puts(json_out_t *o, const char *s)
{
    return out_put(o, s, (uint32_t)strlen(s));
}

/* 写 JSON 字符串：加引号，转义 " 与 \，控制字符 <0x20 用 \u00XX */
static bool out_put_string(json_out_t *o, const char *s)
{
    static const char hex[] = "0123456789ABCDEF";
    char esc[6];
    const unsigned char *p = (const unsigned char *)s;

    if (!out_putc(o, '"')) {
        return false;
    }
    for (; *p != 0U; p++) {
        unsigned char c = *p;

        switch (c) {
        case '"':  if (!out_put(o, "\\\"", 2U)) return false; break;
        case '\\': if (!out_put(o, "\\\\", 2U)) return false; break;
        case '\b': if (!out_put(o, "\\b", 2U)) return false; break;
        case '\f': if (!out_put(o, "\\f", 2U)) return false; break;
        case '\n': if (!out_put(o, "\\n", 2U)) return false; break;
        case '\r': if (!out_put(o, "\\r", 2U)) return false; break;
        case '\t': if (!out_put(o, "\\t", 2U)) return false; break;
        default:
            if (c < 0x20U) {
                esc[0] = '\\';
                esc[1] = 'u';
                esc[2] = '0';
                esc[3] = '0';
                esc[4] = hex[(c >> 4) & 0x0FU];
                esc[5] = hex[c & 0x0FU];
                if (!out_put(o, esc, sizeof(esc))) return false;
            } else if (!out_putc(o, (char)c)) {
                return false;
            }
            break;
        }
    }
    return out_putc(o, '"');
}

/* 写 int32；tmp[16] 足够容纳 "-2147483648" 及结尾（用自带 str_printf） */
static bool out_put_int(json_out_t *o, int32_t v)
{
    char tmp[16];
    int n = str_printf(tmp, (uint32_t)sizeof(tmp), "%ld", (long)v);

    return (n > 0) ? out_put(o, tmp, (uint32_t)n) : false;
}

/*
 * 写 float：NaN/Inf 不是合法 JSON 数字，统一输出 null。
 * 不依赖 libc 浮点 printf：按 7 位有效数字（float 约 7 位十进制精度）
 * 自行拼装，指数 < -4 或 >= 7 时改用科学计数法（与 %g 的取舍一致）。
 * 例：3.25 -> "3.25"；25.0 -> "25"；0.001 -> "0.001"；1e8 -> "1e8"。
 */
static bool out_put_float(json_out_t *o, float v)
{
    char tmp[40];
    char rev[8];                    /* 逆序的十进制数字 */
    uint32_t nr = 0U;
    uint32_t start = 0U;
    uint32_t n;
    uint32_t i;
    uint32_t p = 0U;
    int32_t exp10 = 0;
    bool neg = (v < 0.0f);
    float a = neg ? -v : v;
    uint32_t m;

    if (!isfinite((double)v)) {
        return out_put(o, "null", 4U);      /* NaN/Inf 不是合法 JSON 数字 */
    }
    if (a == 0.0f) {
        return out_put(o, "0", 1U);
    }
    /* 归一化到 [1,10)，同时累计十进制指数 */
    while (a >= 10.0f) {
        a *= 0.1f;
        exp10++;
    }
    while (a < 1.0f) {
        a *= 10.0f;
        exp10--;
    }
    /* 取 7 位有效数字并四舍五入；进位到 1e7 时回退一位 */
    m = (uint32_t)(a * 1000000.0f + 0.5f);
    if (m >= 10000000U) {
        m /= 10U;
        exp10++;
    }
    /* 拆分数字（逆序）并去掉尾随 0，至少保留 1 位 */
    while (m > 0U) {
        rev[nr++] = (char)('0' + (m % 10U));
        m /= 10U;
    }
    while (start + 1U < nr && rev[start] == '0') {
        start++;
    }
    n = nr - start;

    if (neg) {
        tmp[p++] = '-';
    }
    if (exp10 < -4 || exp10 >= 7) {
        /* 科学计数法：d[.ddd]e[-]X */
        tmp[p++] = rev[nr - 1U];
        if (n > 1U) {
            tmp[p++] = '.';
            for (i = 1U; i < n; i++) {
                tmp[p++] = rev[nr - 1U - i];
            }
        }
        p += (uint32_t)str_printf(&tmp[p], (uint32_t)(sizeof(tmp) - p),
                                  "e%d", (int)exp10);
    } else if (exp10 >= 0) {
        uint32_t ip = (uint32_t)exp10 + 1U;     /* 小数点前的位数 */

        if (n <= ip) {
            for (i = 0U; i < n; i++) {
                tmp[p++] = rev[nr - 1U - i];
            }
            for (i = n; i < ip; i++) {
                tmp[p++] = '0';
            }
        } else {
            for (i = 0U; i < ip; i++) {
                tmp[p++] = rev[nr - 1U - i];
            }
            tmp[p++] = '.';
            for (i = ip; i < n; i++) {
                tmp[p++] = rev[nr - 1U - i];
            }
        }
    } else {
        /* 0.000ddd */
        tmp[p++] = '0';
        tmp[p++] = '.';
        for (i = 0U; i < (uint32_t)(-exp10 - 1); i++) {
            tmp[p++] = '0';
        }
        for (i = 0U; i < n; i++) {
            tmp[p++] = rev[nr - 1U - i];
        }
    }
    tmp[p] = '\0';
    return out_put(o, tmp, p);
}

static bool var_write(const var_t *v, json_out_t *o, uint32_t depth);

/*
 * 序列化数组：[elem0,elem1,...]。
 * vec 元素必须是 var_t（elem_size 不符直接失败），逐个 var_write 递归。
 */
static bool array_write(const vec_t *arr, json_out_t *o, uint32_t depth)
{
    uint32_t i;

    if (arr->elem_size != sizeof(var_t)) {
        return false;                       /* 元素不是 var_t，无法序列化 */
    }
    if (!out_putc(o, '[')) {
        return false;
    }
    for (i = 0U; i < arr->len; i++) {
        const var_t *elem = (const var_t *)vec_at(arr, i);

        if (i > 0U && !out_putc(o, ',')) {
            return false;
        }
        if (!var_write(elem, o, depth + 1U)) {
            return false;
        }
    }
    return out_putc(o, ']');
}

/*
 * 序列化对象：{"key":value,...}。
 * map 的 value 必须是 var_t*，NULL 值输出 null；
 * map 迭代按键升序，保证同一份数据每次输出文本稳定（便于比对/缓存）。
 */
static bool object_write(const map_t *obj, json_out_t *o, uint32_t depth)
{
    map_iter_t it;
    map_item_t item;
    bool first = true;

    if (!out_putc(o, '{')) {
        return false;
    }
    map_iter_begin(obj, &it);
    while (map_iter_next(&it, &item)) {
        if (!first && !out_putc(o, ',')) {
            return false;
        }
        first = false;
        if (!out_put_string(o, item.key)) {
            return false;
        }
        if (!out_putc(o, ':')) {
            return false;
        }
        if (item.value != NULL) {
            if (!var_write((const var_t *)item.value, o, depth + 1U)) {
                return false;
            }
        } else if (!out_put(o, "null", 4U)) {
            return false;
        }
    }
    return out_putc(o, '}');
}

/*
 * 序列化单个 var：
 *   NONE->null  BOOL->true/false  INT/FLOAT->数字  STRING->带引号转义串
 *   ARRAY/OBJECT 为 NULL 时输出 null，否则递归；
 *   深度超过 VAR_JSON_MAX_DEPTH 视为失败（防止环形引用无限递归）。
 */
static bool var_write(const var_t *v, json_out_t *o, uint32_t depth)
{
    if (v == NULL || depth > VAR_JSON_MAX_DEPTH) {
        return false;
    }

    switch (v->type) {
    case VAR_NONE:
        return out_put(o, "null", 4U);
    case VAR_BOOL:
        return out_puts(o, v->value.b ? "true" : "false");
    case VAR_INT:
        return out_put_int(o, v->value.i);
    case VAR_FLOAT:
        return out_put_float(o, v->value.f);
    case VAR_STRING:
        return out_put_string(o, (v->value.s != NULL) ? v->value.s : "");
    case VAR_ARRAY:
        return (v->value.arr != NULL) ? array_write(v->value.arr, o, depth + 1U)
                                      : out_put(o, "null", 4U);
    case VAR_OBJECT:
        return (v->value.obj != NULL) ? object_write(v->value.obj, o, depth + 1U)
                                      : out_put(o, "null", 4U);
    default:
        return false;
    }
}

/*
 * var -> JSON 文本。
 * 成功返回堆上的 '\0' 结尾字符串（ros_mem 分配，需 var_free_json 释放）；
 * v 为 NULL、深度超限或内存不足返回 NULL。
 */
char *var_to_json(const var_t *v)
{
    json_out_t o = { NULL, 0U, 0U };

    if (!var_write(v, &o, 0U)) {
        ros_mem_free(o.buf);
        return NULL;
    }
    return o.buf;
}

/* 释放 var_to_json 返回的文本（本质就是 ros_mem_free） */
void var_free_json(char *text)
{
    ros_mem_free(text);
}

/* ============================== 反序列化 ============================== */

/*
 * 解析游标：p 指向下一个待消费字符，所有 parse_* 都原地前移 p。
 * 输入必须是 '\0' 结尾，因此不需要额外的结束指针。
 */
typedef struct {
    const char *p;
} json_in_t;

static bool is_digit(char c)
{
    return (c >= '0' && c <= '9');
}

static void skip_ws(json_in_t *in)
{
    while (*in->p == ' ' || *in->p == '\t' ||
           *in->p == '\n' || *in->p == '\r') {
        in->p++;
    }
}

static bool parse_literal(json_in_t *in, const char *lit)
{
    size_t n = strlen(lit);

    if (!str_starts_with(in->p, lit)) {
        return false;
    }
    in->p += n;
    return true;
}

/*
 * 写入一个字节：cap 含结尾 '\0' 的位置，因此仅在 stored+1 < cap 时写入，
 * 超出部分只丢弃（与 strncpy 截断语义一致），stored 始终不超过 cap-1。
 */
static void emit_byte(char *buf, uint32_t cap, uint32_t *stored, char c)
{
    if (*stored + 1U < cap) {
        buf[*stored] = c;
        (*stored)++;
    }
}

/* UTF-8 编码并写入 */
static void emit_utf8(char *buf, uint32_t cap, uint32_t *stored, uint32_t cp)
{
    if (cp < 0x80U) {
        emit_byte(buf, cap, stored, (char)cp);
    } else if (cp < 0x800U) {
        emit_byte(buf, cap, stored, (char)(0xC0U | (cp >> 6)));
        emit_byte(buf, cap, stored, (char)(0x80U | (cp & 0x3FU)));
    } else if (cp < 0x10000U) {
        emit_byte(buf, cap, stored, (char)(0xE0U | (cp >> 12)));
        emit_byte(buf, cap, stored, (char)(0x80U | ((cp >> 6) & 0x3FU)));
        emit_byte(buf, cap, stored, (char)(0x80U | (cp & 0x3FU)));
    } else {
        emit_byte(buf, cap, stored, (char)(0xF0U | (cp >> 18)));
        emit_byte(buf, cap, stored, (char)(0x80U | ((cp >> 12) & 0x3FU)));
        emit_byte(buf, cap, stored, (char)(0x80U | ((cp >> 6) & 0x3FU)));
        emit_byte(buf, cap, stored, (char)(0x80U | (cp & 0x3FU)));
    }
}

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

static bool read_hex4(json_in_t *in, uint32_t *out)
{
    uint32_t v = 0U;
    uint32_t i;

    for (i = 0U; i < 4U; i++) {
        int h = hex_value(in->p[i]);

        if (h < 0) {
            return false;
        }
        v = (v << 4) | (uint32_t)h;
    }
    in->p += 4;
    *out = v;
    return true;
}

/*
 * 解析反斜杠后的转义序列（调用时 in->p 指向反斜杠后一个字符）。
 * 支持 \" \\ \/ \b \f \n \r \t；\uXXXX 解码为 UTF-8，
 * 遇高位代理（D800-DBFF）会尝试拼接紧随的低位代理（DC00-DFFF）。
 */
static bool parse_escape(json_in_t *in, char *buf, uint32_t cap, uint32_t *stored)
{
    char c = *in->p;

    switch (c) {
    case '"':  emit_byte(buf, cap, stored, '"');  break;
    case '\\': emit_byte(buf, cap, stored, '\\'); break;
    case '/':  emit_byte(buf, cap, stored, '/');  break;
    case 'b':  emit_byte(buf, cap, stored, '\b'); break;
    case 'f':  emit_byte(buf, cap, stored, '\f'); break;
    case 'n':  emit_byte(buf, cap, stored, '\n'); break;
    case 'r':  emit_byte(buf, cap, stored, '\r'); break;
    case 't':  emit_byte(buf, cap, stored, '\t'); break;
    case 'u': {
        uint32_t cp;

        in->p++;
        if (!read_hex4(in, &cp)) {
            return false;
        }
        /* 代理对：高位 + 紧跟的 \u 低位 */
        if (cp >= 0xD800U && cp <= 0xDBFFU &&
            in->p[0] == '\\' && in->p[1] == 'u') {
            const char *save = in->p;
            uint32_t low;

            in->p += 2;
            if (read_hex4(in, &low) && low >= 0xDC00U && low <= 0xDFFFU) {
                cp = 0x10000U + ((cp - 0xD800U) << 10) + (low - 0xDC00U);
            } else {
                in->p = save;               /* 非法低位：按单个码点处理 */
            }
        }
        emit_utf8(buf, cap, stored, cp);
        return true;
    }
    default:
        return false;
    }
    in->p++;
    return true;
}

/*
 * 解析带引号的字符串到 buf（截断到 cap-1，结尾补 '\0'）。
 * 处理转义与 UTF-8 解码；遇未转义控制字符或未闭合返回 false。
 * out_len 非 NULL 时返回实际写入的字节数。
 */
static bool parse_string(json_in_t *in, char *buf, uint32_t cap, uint32_t *out_len)
{
    uint32_t stored = 0U;

    if (*in->p != '"') {
        return false;
    }
    in->p++;
    while (*in->p != '\0') {
        unsigned char c = (unsigned char)*in->p;

        if (c == '"') {
            in->p++;
            buf[stored] = '\0';
            if (out_len != NULL) {
                *out_len = stored;
            }
            return true;
        }
        if (c == '\\') {
            in->p++;
            if (!parse_escape(in, buf, cap, &stored)) {
                return false;
            }
            continue;
        }
        if (c < 0x20U) {
            return false;                   /* 未转义的控制字符 */
        }
        emit_byte(buf, cap, &stored, (char)c);
        in->p++;
    }
    return false;                           /* 未闭合 */
}

/*
 * 解析 JSON 数字：
 *   - 纯整数且落在 int32 范围内 -> VAR_INT（含 -2147483648 边界）；
 *   - 带小数/指数，或超出 int32 范围 -> VAR_FLOAT（借 strtod 定值）；
 * 调用前 in->p 必须位于 '-' 或数字上；语法不完整返回 false。
 * 注意：strtod 会引入浮点库，嵌入式目标需确认体积可接受。
 */
static bool parse_number(json_in_t *in, var_t *out)
{
    const char *start = in->p;
    uint64_t mag = 0U;
    bool neg = false;
    bool is_float = false;
    bool overflow = false;

    if (*in->p == '-') {
        neg = true;
        in->p++;
    }
    if (!is_digit(*in->p)) {
        return false;
    }
    while (is_digit(*in->p)) {
        uint32_t d = (uint32_t)(*in->p - '0');

        if (mag > (UINT64_MAX - d) / 10U) {
            overflow = true;
        } else {
            mag = mag * 10U + d;
        }
        in->p++;
    }
    if (*in->p == '.') {
        is_float = true;
        in->p++;
        if (!is_digit(*in->p)) {
            return false;
        }
        while (is_digit(*in->p)) {
            in->p++;
        }
    }
    if (*in->p == 'e' || *in->p == 'E') {
        is_float = true;
        in->p++;
        if (*in->p == '+' || *in->p == '-') {
            in->p++;
        }
        if (!is_digit(*in->p)) {
            return false;
        }
        while (is_digit(*in->p)) {
            in->p++;
        }
    }

    if (!is_float && !overflow) {
        if (neg) {
            if (mag <= 2147483648ULL) {
                var_set_int(out, (mag == 2147483648ULL) ? INT32_MIN
                                                        : -(int32_t)mag);
                return true;
            }
        } else if (mag <= 2147483647ULL) {
            var_set_int(out, (int32_t)mag);
            return true;
        }
    }

    /* 超出 int32 或带小数/指数：按浮点解析（自带 str_parse_float，无 libc） */
    {
        char num[48];
        uint32_t len = (uint32_t)(in->p - start);
        float f;

        if (len >= (uint32_t)sizeof(num)) {
            return false;                   /* 数字过长，超出本地缓冲 */
        }
        memcpy(num, start, len);
        num[len] = '\0';
        if (!str_parse_float(num, &f)) {
            return false;
        }
        var_set_float(out, f);
        return true;
    }
}

/*
 * 解析单个标量值并写入 out（跳过前导空白）。
 * 字符串写入调用者提供的 strbuf；数组/对象（'[' / '{'）不支持，返回 false。
 */
static bool parse_scalar(json_in_t *in, var_t *out, char *strbuf, uint32_t strcap)
{
    skip_ws(in);

    switch (*in->p) {
    case 'n':
        if (!parse_literal(in, "null")) {
            return false;
        }
        var_init(out);
        return true;
    case 't':
        if (!parse_literal(in, "true")) {
            return false;
        }
        var_set_bool(out, true);
        return true;
    case 'f':
        if (!parse_literal(in, "false")) {
            return false;
        }
        var_set_bool(out, false);
        return true;
    case '"':
        if (strbuf == NULL || strcap == 0U) {
            return false;
        }
        if (!parse_string(in, strbuf, strcap, NULL)) {
            return false;
        }
        var_set_string(out, strbuf);
        return true;
    default:
        if (*in->p == '-' || is_digit(*in->p)) {
            return parse_number(in, out);
        }
        return false;                       /* 数组/对象不支持 */
    }
}

/*
 * JSON 文本 -> var（仅标量）。
 * 允许首尾空白，但整个输入必须恰好是一个值，否则失败。
 * 字符串结果指向 strbuf（截断到 strcap-1），调用者需保证其生命周期；
 * 解析失败时不修改 *out。
 */
bool var_from_json(const char *text, var_t *out, char *strbuf, uint32_t strcap)
{
    json_in_t in;
    var_t tmp;

    if (text == NULL || out == NULL) {
        return false;
    }
    in.p = text;
    if (!parse_scalar(&in, &tmp, strbuf, strcap)) {
        return false;
    }
    skip_ws(&in);
    if (*in.p != '\0') {
        return false;                       /* 只允许一个 JSON 值 */
    }
    *out = tmp;
    return true;
}
