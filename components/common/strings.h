#ifndef STRINGS_H
#define STRINGS_H

/*
 * strings.h - 通用字符串处理（无动态内存，原地操作）
 *
 * 从项目内各组件反复手写的字符串逻辑中提取：
 *   - 大小写不敏感比较 / 前后缀判断
 *   - 安全拷贝、裁剪首尾空白、去掉行尾 CR/LF
 *   - 严格数字解析、布尔文本解析
 *   - 原地切分、字符大小写转换、跳过空白
 *
 * 约定：所有接口对 NULL 入参安全（除明确要求可写的缓冲外）；
 *       需要写入的接口在 size 为 0 时不做任何写入。
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------- 比较 / 判断 ---------------- */

/* 大小写不敏感比较（等价于 strcasecmp 的布尔版） */
bool str_icmp(const char *a, const char *b);

/* 大小写不敏感比较前 n 个字符 */
bool str_nicmp(const char *a, const char *b, uint32_t n);

/* 前缀 / 后缀判断（大小写敏感） */
bool str_starts_with(const char *s, const char *prefix);
bool str_ends_with(const char *s, const char *suffix);

/* 是否非空且全部为十进制数字 */
bool str_is_digits(const char *s);

/* ---------------- 拷贝 / 编辑 ---------------- */

/*
 * 安全拷贝：最多写 size-1 字节并补 '\0'。
 * 返回写入长度（不含 '\0'）；src 为 NULL 时返回 0 并写空串。
 */
uint32_t str_copy(char *dst, uint32_t size, const char *src);

/*
 * 复制字符串到堆（等价 strdup）。
 * 返回新分配的 '\0' 结尾副本；s 为 NULL 或内存不足时返回 NULL。
 * 注意：调用者必须使用 ros_mem_free() 手动释放。
 */
char *str_dup(const char *s);

/*
 * 去掉首尾空白（空格 / \t / \r / \n），原地修改。
 * 返回处理后长度。
 */
uint32_t str_trim(char *s);

/* 去掉行尾的 \r / \n（原地），返回处理后长度 */
uint32_t str_strip_eol(char *s);

/* 原地转大写 / 小写，返回字符串长度 */
uint32_t str_to_upper(char *s);
uint32_t str_to_lower(char *s);

/* 单字符大小写转换（非字母原样返回） */
static inline char str_upper_char(char c)
{
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}
static inline char str_lower_char(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

/* 返回跳过前导空白后的指针（不修改原串） */
static inline const char *str_skip_ws(const char *s)
{
    if (s == NULL) {
        return NULL;
    }
    while (*s == ' ' || *s == '\t') {
        s++;
    }
    return s;
}

/* ---------------- 解析 ---------------- */

/* 严格解析十进制整数：必须整串匹配，允许 +/- 前缀 */
bool str_parse_int(const char *s, int32_t *out);

/* 严格解析十进制无符号整数：必须整串匹配，不接受 '-' */
bool str_parse_uint(const char *s, uint32_t *out);

/*
 * 严格解析十进制浮点：必须整串匹配。
 * 语法：可选 '+'/'-'、整数 / 小数、可选 e/E 指数（指数可带符号）。
 * 允许前导 '.'（如 ".5"）与尾随 '.'（如 "1."），但小数点前后至少有一位数字。
 * 纯整数运算累加（至多 9 位有效数字），溢出 / 下溢返回 false。
 */
bool str_parse_float(const char *s, float *out);

/* 解析布尔文本：1/0、on/off、true/false、yes/no（大小写不敏感） */
bool str_parse_bool(const char *s, bool *out);

/*
 * 原地切分：把分隔符替换为 '\0'，items 保存各段起点。
 * 连续分隔符产生的空段会被跳过。
 * 返回实际段数；items 或 s 为 NULL 时返回 0。
 */
uint32_t str_split(char *s, char delim, char **items, uint32_t max_items);

/* ---------------- 格式化输出 ---------------- */

/*
 * 精简 printf（不依赖 libc 的 vfprintf）：
 *   转换：%s %c %d %i %u %x %X %p %%
 *   标志：'-'（左对齐）、'0'（补零）
 *   宽度：十进制数字或 '*'；精度：'.数字' 或 '.*'（字符串截断、整数最小位数）
 *   长度：'l'（long / unsigned long）
 * 不支持浮点（%f/%g/%e），未知转换按 "%<字符>" 原样输出。
 *
 * 返回值同 C99：本应写入的字符数（不含结尾 '\0'），超长时截断。
 * cap 为 0 时不写 buf，仅返回长度；buf 非空且 cap>0 时总是以 '\0' 结尾。
 */
int str_vsnprintf(char *buf, uint32_t cap, const char *fmt, va_list ap);

/* str_vsnprintf 的可变参数版本 */
int str_printf(char *buf, uint32_t cap, const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif /* STRINGS_H */
