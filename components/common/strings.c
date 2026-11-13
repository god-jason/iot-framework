/*
 * strings.c - 通用字符串处理实现
 */

#include "strings.h"
#include "ros.h"
#include "common.h"

#include <stdlib.h>

/* ---------------- 比较 / 判断 ---------------- */

char str_lower_char(char c)
{
    if (c >= 'A' && c <= 'Z') {
        return (char)(c - 'A' + 'a');
    }
    return c;
}

char str_upper_char(char c)
{
    if (c >= 'a' && c <= 'z') {
        return (char)(c - 'a' + 'A');
    }
    return c;
}

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

const char *str_skip_ws(const char *s)
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

bool str_parse_int(const char *s, int32_t *out)
{
    char *end = NULL;
    long v;

    if (s == NULL || out == NULL || s[0] == '\0') {
        return false;
    }
    v = strtol(s, &end, 10);
    if (end == s || *end != '\0') {
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
    v = strtoul(s, &end, 10);
    if (end == s || *end != '\0') {
        return false;
    }
    *out = (uint32_t)v;
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
