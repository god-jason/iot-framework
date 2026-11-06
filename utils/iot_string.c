#include "iot_string.h"

iot_size_t iot_string_length(const char *str)
{
    iot_size_t n = 0;
    if (str == NULL) {
        return 0;
    }
    while (str[n] != '\0') {
        n++;
    }
    return n;
}

char *iot_string_copy(char *dst, const char *src)
{
    char *d = dst;
    if (dst == NULL || src == NULL) {
        return dst;
    }
    while (*src != '\0') {
        *d++ = *src++;
    }
    *d = '\0';
    return dst;
}

char *iot_string_ncopy(char *dst, const char *src, iot_size_t size)
{
    char *d = dst;
    iot_size_t i;

    if (dst == NULL || src == NULL || size == 0) {
        return dst;
    }
    for (i = 0; i < (size - 1); i++) {
        if (src[i] == '\0') {
            break;
        }
        d[i] = src[i];
    }
    d[i] = '\0';
    return dst;
}

int iot_string_compare(const char *a, const char *b)
{
    if (a == b) {
        return 0;
    }
    if (a == NULL) {
        return -1;
    }
    if (b == NULL) {
        return 1;
    }
    while (*a != '\0' && *a == *b) {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

int iot_string_ncompare(const char *a, const char *b, iot_size_t len)
{
    iot_size_t i;
    if (a == b || len == 0) {
        return 0;
    }
    for (i = 0; i < len; i++) {
        if (a[i] != b[i]) {
            return (unsigned char)a[i] - (unsigned char)b[i];
        }
        if (a[i] == '\0') {
            break;
        }
    }
    return 0;
}

char *iot_string_find(const char *str, const char *substr)
{
    if (str == NULL || substr == NULL) {
        return NULL;
    }
    if (*substr == '\0') {
        return (char *)str;
    }
    while (*str != '\0') {
        const char *s = str;
        const char *p = substr;
        while (*p != '\0' && *s == *p) {
            s++;
            p++;
        }
        if (*p == '\0') {
            return (char *)str;
        }
        str++;
    }
    return NULL;
}

char *iot_string_find_char(const char *str, char ch)
{
    if (str == NULL) {
        return NULL;
    }
    while (*str != '\0') {
        if (*str == ch) {
            return (char *)str;
        }
        str++;
    }
    return NULL;
}

int iot_string_split(char *str, char delimiter,
                     char **items, uint32_t capacity,
                     uint32_t *count)
{
    uint32_t n = 0;
    char *token;

    if (count != NULL) {
        *count = 0;
    }
    if (str == NULL || items == NULL || capacity == 0) {
        return IOT_ERR_PARAM;
    }

    token = str;
    while (*str != '\0') {
        if (*str == delimiter) {
            if (n < capacity) {
                items[n] = token;
                n++;
            }
            *str = '\0';
            token = str + 1;
        }
        str++;
    }
    /* 末尾残留字段（即使为空串也保留，便于处理 "a,b," 这类输入） */
    if (n < capacity && (*token != '\0' || token <= str)) {
        items[n] = token;
        n++;
    }

    if (count != NULL) {
        *count = n;
    }
    return (n <= capacity) ? IOT_OK : IOT_ERR_OVERFLOW;
}

int iot_string_to_int(const char *str, int32_t *value)
{
    int32_t result = 0;
    int negative = 0;
    int started = 0;

    if (str == NULL || value == NULL) {
        return IOT_ERR_PARAM;
    }

    if (*str == '-') {
        negative = 1;
        str++;
    } else if (*str == '+') {
        str++;
    }

    while (*str != '\0') {
        if (*str < '0' || *str > '9') {
            break;
        }
        result = result * 10 + (*str - '0');
        started = 1;
        str++;
    }

    if (!started) {
        return IOT_ERR_INVALID;
    }
    *value = negative ? -result : result;
    return IOT_OK;
}

int iot_string_to_uint(const char *str, uint32_t *value)
{
    uint32_t result = 0;
    int started = 0;

    if (str == NULL || value == NULL) {
        return IOT_ERR_PARAM;
    }

    while (*str != '\0') {
        if (*str < '0' || *str > '9') {
            break;
        }
        result = result * 10 + (uint32_t)(*str - '0');
        started = 1;
        str++;
    }

    if (!started) {
        return IOT_ERR_INVALID;
    }
    *value = result;
    return IOT_OK;
}

int iot_string_to_float(const char *str, float *value)
{
    float result = 0.0f;
    float fraction = 0.0f;
    float divisor = 1.0f;
    int negative = 0;
    int in_fraction = 0;
    int started = 0;

    if (str == NULL || value == NULL) {
        return IOT_ERR_PARAM;
    }

    if (*str == '-') {
        negative = 1;
        str++;
    } else if (*str == '+') {
        str++;
    }

    while (*str != '\0') {
        if (*str == '.') {
            if (in_fraction) {
                break;
            }
            in_fraction = 1;
        } else if (*str >= '0' && *str <= '9') {
            started = 1;
            if (in_fraction) {
                fraction = fraction * 10.0f + (float)(*str - '0');
                divisor *= 10.0f;
            } else {
                result = result * 10.0f + (float)(*str - '0');
            }
        } else {
            break;
        }
        str++;
    }

    if (!started) {
        return IOT_ERR_INVALID;
    }
    result += fraction / divisor;
    *value = negative ? -result : result;
    return IOT_OK;
}