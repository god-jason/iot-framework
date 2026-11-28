/*
 * strings.c - 通用字符串处理实现
 */

#include "strings.h"
#include "ros.h"
#include "common.h"

#include <stdlib.h>
#include <errno.h>
#include <stdint.h>
#include <float.h>

/* ---------------- 比较 / 判断 ---------------- */

bool str_icmp(const char *a, const char *b)
{
    if (a == NULL || b == NULL) {
        return a == b;
    }
    while (*a != '\0' && *b != '\0') {
        if (str_lower_char(*a) != str_lower_char(*b)) {
            return false;
        }
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

bool str_nicmp(const char *a, const char *b, uint32_t n)
{
    uint32_t i;

    if (a == NULL || b == NULL) {
        return a == b;
    }
    for (i = 0U; i < n; i++) {
        if (str_lower_char(a[i]) != str_lower_char(b[i])) {
            return false;
        }
        if (a[i] == '\0') {
            return true;
        }
    }
    return true;
}

bool str_starts_with(const char *s, const char *prefix)
{
    size_t n;

    if (s == NULL || prefix == NULL) {
        return false;
    }
    n = strlen(prefix);
    return strncmp(s, prefix, n) == 0;
}

bool str_ends_with(const char *s, const char *suffix)
{
    size_t slen;
    size_t n;

    if (s == NULL || suffix == NULL) {
        return false;
    }
    slen = strlen(s);
    n = strlen(suffix);
    if (n > slen) {
        return false;
    }
    return strcmp(&s[slen - n], suffix) == 0;
}

bool str_is_digits(const char *s)
{
    if (s == NULL || s[0] == '\0') {
        return false;
    }
    while (*s != '\0') {
        if (*s < '0' || *s > '9') {
            return false;
        }
        s++;
    }
    return true;
}

/* ---------------- 拷贝 / 编辑 ---------------- */

uint32_t str_copy(char *dst, uint32_t size, const char *src)
{
    uint32_t n = 0U;

    if (dst == NULL || size == 0U) {
        return 0U;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return 0U;
    }
    while (src[n] != '\0' && n + 1U < size) {
        dst[n] = src[n];
        n++;
    }
    dst[n] = '\0';
    return n;
}

char *str_dup(const char *s)
{
    size_t len;
    char *p;

    if (s == NULL) {
        return NULL;
    }
    len = strlen(s);
    p = (char *)ros_mem_malloc(len + 1U);
    if (p == NULL) {
        return NULL;
    }
    memcpy(p, s, len + 1U);
    return p;
}

uint32_t str_trim(char *s)
{
    uint32_t len;
    uint32_t begin = 0U;

    if (s == NULL) {
        return 0U;
    }
    len = (uint32_t)strlen(s);
    while (begin < len &&
           (s[begin] == ' ' || s[begin] == '\t' ||
            s[begin] == '\r' || s[begin] == '\n')) {
        begin++;
    }
    while (len > begin &&
           (s[len - 1U] == ' ' || s[len - 1U] == '\t' ||
            s[len - 1U] == '\r' || s[len - 1U] == '\n')) {
        len--;
    }
    if (begin != 0U) {
        memmove(s, &s[begin], len - begin);
    }
    s[len - begin] = '\0';
    return len - begin;
}

uint32_t str_strip_eol(char *s)
{
    uint32_t len;

    if (s == NULL) {
        return 0U;
    }
    len = (uint32_t)strlen(s);
    while (len > 0U && (s[len - 1U] == '\r' || s[len - 1U] == '\n')) {
        s[--len] = '\0';
    }
    return len;
}

uint32_t str_to_upper(char *s)
{
    uint32_t n = 0U;

    if (s == NULL) {
        return 0U;
    }
    while (s[n] != '\0') {
        s[n] = str_upper_char(s[n]);
        n++;
    }
    return n;
}

uint32_t str_to_lower(char *s)
{
    uint32_t n = 0U;

    if (s == NULL) {
        return 0U;
    }
    while (s[n] != '\0') {
        s[n] = str_lower_char(s[n]);
        n++;
    }
    return n;
}

/* ---------------- 解析 ---------------- */

bool str_parse_int(const char *s, int32_t *out)
{
    char *end = NULL;
    long v;

    if (s == NULL || out == NULL || s[0] == '\0') {
        return false;
    }
    errno = 0;
    v = strtol(s, &end, 10);
    if (end == s || *end != '\0') {
        return false;
    }
    /* 溢出（ERANGE）或在 long 比 int32 更宽的平台超出 int32 范围 */
    if (errno == ERANGE || v > (long)INT32_MAX || v < (long)INT32_MIN) {
        return false;
    }
    *out = (int32_t)v;
    return true;
}

bool str_parse_uint(const char *s, uint32_t *out)
{
    char *end = NULL;
    unsigned long v;

    if (s == NULL || out == NULL || s[0] == '\0' || s[0] == '-') {
        return false;
    }
    errno = 0;
    v = strtoul(s, &end, 10);
    if (end == s || *end != '\0') {
        return false;
    }
    /* 溢出（ERANGE）或在 unsigned long 比 uint32 更宽的平台超出 uint32 范围 */
    if (errno == ERANGE || v > (unsigned long)UINT32_MAX) {
        return false;
    }
    *out = (uint32_t)v;
    return true;
}

bool str_parse_float(const char *s, float *out)
{
    const char *p;
    uint64_t mant = 0U;
    uint32_t sig = 0U;                 /* 已累计的有效数字位数（至多 9） */
    int32_t exp10 = 0;
    bool neg = false;
    bool digits = false;
    float v;

    if (s == NULL || out == NULL || s[0] == '\0') {
        return false;
    }
    p = s;
    if (*p == '+' || *p == '-') {
        neg = (*p == '-');
        p++;
    }
    /* 整数部分：前导 0 不计入有效位；超出 9 位的整数位补偿到指数 */
    while (*p >= '0' && *p <= '9') {
        digits = true;
        if (sig < 9U) {
            if (sig > 0U || *p != '0') {
                mant = mant * 10U + (uint64_t)(*p - '0');
                sig++;
            }
        } else {
            exp10++;
        }
        p++;
    }
    if (*p == '.') {
        p++;
        /* 小数位全部计入指数；仅累计前 9 位有效数字 */
        while (*p >= '0' && *p <= '9') {
            digits = true;
            if (sig < 9U) {
                if (sig > 0U || *p != '0') {
                    mant = mant * 10U + (uint64_t)(*p - '0');
                    sig++;
                }
                exp10--;
            }
            p++;
        }
    }
    if (!digits) {
        return false;
    }
    if (*p == 'e' || *p == 'E') {
        const char *q = p + 1;
        int32_t e = 0;
        bool eneg = false;

        if (*q == '+' || *q == '-') {
            eneg = (*q == '-');
            q++;
        }
        if (*q < '0' || *q > '9') {
            return false;
        }
        while (*q >= '0' && *q <= '9') {
            if (e < 1000) {
                e = e * 10 + (*q - '0');
            }
            q++;
        }
        exp10 += eneg ? -e : e;
        p = q;
    }
    if (*p != '\0') {
        return false;
    }
    if (mant == 0U) {
        *out = neg ? -0.0f : 0.0f;
        return true;
    }
    /* 数量级明显越界时提前失败，避免长循环 */
    if (exp10 > 40 || exp10 < -50) {
        return false;
    }
    v = (float)mant;
    while (exp10 > 0) {
        v *= 10.0f;
        exp10--;
    }
    while (exp10 < 0) {
        v *= 0.1f;
        exp10++;
    }
    if (v > FLT_MAX) {
        return false;
    }
    /* 下溢到 0 是合法的极小值（如 1e-45），不是语法错误 */
    *out = neg ? -v : v;
    return true;
}

bool str_parse_bool(const char *s, bool *out)
{
    if (s == NULL || out == NULL) {
        return false;
    }
    if (str_icmp(s, "1") || str_icmp(s, "on") || str_icmp(s, "true") ||
        str_icmp(s, "yes")) {
        *out = true;
        return true;
    }
    if (str_icmp(s, "0") || str_icmp(s, "off") || str_icmp(s, "false") ||
        str_icmp(s, "no")) {
        *out = false;
        return true;
    }
    return false;
}

uint32_t str_split(char *s, char delim, char **items, uint32_t max_items)
{
    uint32_t count = 0U;
    char *p;

    if (s == NULL || items == NULL || max_items == 0U) {
        return 0U;
    }
    p = s;
    while (*p != '\0' && count < max_items) {
        char *start = p;

        while (*p != '\0' && *p != delim) {
            p++;
        }
        if (*p == delim) {
            *p++ = '\0';
        }
        if (*start != '\0') {
            items[count++] = start;
        }
    }
    return count;
}

/* ---------------- 格式化输出 ---------------- */

typedef struct {
    char    *buf;
    uint32_t cap;
    uint32_t pos;       /* 已尝试写入的字符数 */
    int      total;     /* 本应写入的总长度（C99 返回值） */
} str_fmt_t;

static void str_fmt_putc(str_fmt_t *f, char c)
{
    if (f->buf != NULL && f->pos + 1U < f->cap) {
        f->buf[f->pos] = c;
    }
    f->pos++;
    f->total++;
}

static void str_fmt_pad(str_fmt_t *f, char c, uint32_t n)
{
    while (n > 0U) {
        str_fmt_putc(f, c);
        n--;
    }
}

/* 十进制无符号转字符（逆序写入 tmp） */
static uint32_t str_fmt_utoa10(char *tmp, unsigned long v)
{
    uint32_t n = 0U;

    do {
        tmp[n++] = (char)('0' + (char)(v % 10U));
        v /= 10U;
    } while (v != 0U);
    return n;
}

/* 十六进制无符号转字符（逆序写入 tmp） */
static uint32_t str_fmt_utoa16(char *tmp, unsigned long v, bool upper)
{
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    uint32_t n = 0U;

    do {
        tmp[n++] = digits[v & 0x0FU];
        v >>= 4;
    } while (v != 0U);
    return n;
}

/*
 * 按宽度 / 精度 / 标志输出一个整数。
 * prefix 为 "0x" 等前缀（%p 用），neg 表示负数符号。
 */
static void str_fmt_number(str_fmt_t *f, unsigned long uv, bool hex, bool upper,
                           bool neg, bool left, bool zero,
                           uint32_t width, int32_t prec, const char *prefix)
{
    char tmp[32];
    uint32_t n = hex ? str_fmt_utoa16(tmp, uv, upper)
                     : str_fmt_utoa10(tmp, uv);
    uint32_t plen = 0U;
    uint32_t zeros = 0U;
    uint32_t len;
    uint32_t i;

    while (prefix != NULL && prefix[plen] != '\0') {
        plen++;
    }
    if (prec >= 0 && (uint32_t)prec > n) {
        zeros = (uint32_t)prec - n;             /* 精度：最小位数 */
    } else if (zero && prec < 0) {
        uint32_t base = plen + (neg ? 1U : 0U) + n;

        if (width > base) {
            zeros = width - base;               /* '0' 标志：补到宽度 */
        }
    }
    len = plen + (neg ? 1U : 0U) + zeros + n;
    if (!left && len < width) {
        str_fmt_pad(f, ' ', width - len);
    }
    if (neg) {
        str_fmt_putc(f, '-');
    }
    for (i = 0U; i < plen; i++) {
        str_fmt_putc(f, prefix[i]);
    }
    str_fmt_pad(f, '0', zeros);
    while (n > 0U) {
        str_fmt_putc(f, tmp[--n]);
    }
    if (left && len < width) {
        str_fmt_pad(f, ' ', width - len);
    }
}

int str_vsnprintf(char *buf, uint32_t cap, const char *fmt, va_list ap)
{
    str_fmt_t f;
    const char *p;

    f.buf = buf;
    f.cap = cap;
    f.pos = 0U;
    f.total = 0;
    if (buf != NULL && cap > 0U) {
        buf[0] = '\0';
    }
    if (fmt == NULL || (buf == NULL && cap > 0U)) {
        return -1;
    }

    p = fmt;
    while (*p != '\0') {
        bool left = false;
        bool zero = false;
        bool is_long = false;
        uint32_t width = 0U;
        int32_t prec = -1;
        char conv;

        if (*p != '%') {
            str_fmt_putc(&f, *p++);
            continue;
        }
        p++;

        /* 标志 */
        while (*p == '-' || *p == '0') {
            if (*p == '-') {
                left = true;
            } else {
                zero = true;
            }
            p++;
        }

        /* 宽度 */
        if (*p == '*') {
            int w = va_arg(ap, int);

            p++;
            if (w < 0) {
                left = true;
                w = -w;
            }
            width = (uint32_t)w;
        } else {
            while (*p >= '0' && *p <= '9') {
                width = width * 10U + (uint32_t)(*p - '0');
                p++;
            }
        }

        /* 精度 */
        if (*p == '.') {
            p++;
            if (*p == '*') {
                int v = va_arg(ap, int);

                p++;
                prec = (v >= 0) ? v : -1;
            } else {
                prec = 0;
                while (*p >= '0' && *p <= '9') {
                    prec = prec * 10 + (*p - '0');
                    p++;
                }
            }
        }

        /* 长度修饰（仅支持 long） */
        if (*p == 'l') {
            is_long = true;
            p++;
        }

        conv = *p;
        if (conv == '\0') {
            break;
        }
        p++;

        switch (conv) {
        case '%':
            str_fmt_putc(&f, '%');
            break;

        case 'c': {
            char ch = (char)va_arg(ap, int);

            if (!left && width > 1U) {
                str_fmt_pad(&f, ' ', width - 1U);
            }
            str_fmt_putc(&f, ch);
            if (left && width > 1U) {
                str_fmt_pad(&f, ' ', width - 1U);
            }
            break;
        }

        case 's': {
            const char *s = va_arg(ap, const char *);
            uint32_t n = 0U;
            uint32_t i;

            if (s == NULL) {
                s = "(null)";
            }
            while (s[n] != '\0' && (prec < 0 || n < (uint32_t)prec)) {
                n++;
            }
            if (!left && width > n) {
                str_fmt_pad(&f, ' ', width - n);
            }
            for (i = 0U; i < n; i++) {
                str_fmt_putc(&f, s[i]);
            }
            if (left && width > n) {
                str_fmt_pad(&f, ' ', width - n);
            }
            break;
        }

        case 'd':
        case 'i': {
            long v = is_long ? va_arg(ap, long) : (long)va_arg(ap, int);
            bool neg = (v < 0);
            unsigned long uv = neg ? ((unsigned long)(-(v + 1)) + 1UL)
                                   : (unsigned long)v;

            str_fmt_number(&f, uv, false, false, neg, left, zero,
                           width, prec, NULL);
            break;
        }

        case 'u': {
            unsigned long uv = is_long ? va_arg(ap, unsigned long)
                                       : (unsigned long)va_arg(ap, unsigned int);

            str_fmt_number(&f, uv, false, false, false, left, zero,
                           width, prec, NULL);
            break;
        }

        case 'x':
        case 'X': {
            unsigned long uv = is_long ? va_arg(ap, unsigned long)
                                       : (unsigned long)va_arg(ap, unsigned int);

            str_fmt_number(&f, uv, true, conv == 'X', false, left, zero,
                           width, prec, NULL);
            break;
        }

        case 'p': {
            unsigned long uv = (unsigned long)(uintptr_t)va_arg(ap, void *);

            str_fmt_number(&f, uv, true, false, false, left, zero,
                           width, prec, "0x");
            break;
        }

        default:                            /* 未知转换：原样输出 */
            str_fmt_putc(&f, '%');
            str_fmt_putc(&f, conv);
            break;
        }
    }

    if (buf != NULL && cap > 0U) {
        uint32_t end = (f.pos < cap) ? f.pos : (cap - 1U);

        buf[end] = '\0';
    }
    return f.total;
}

int str_printf(char *buf, uint32_t cap, const char *fmt, ...)
{
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = str_vsnprintf(buf, cap, fmt, ap);
    va_end(ap);
    return n;
}
