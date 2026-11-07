#include "iot_base64.h"

static const char b64_alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static int b64_value(char c)
{
    if (c >= 'A' && c <= 'Z') {
        return c - 'A';
    }
    if (c >= 'a' && c <= 'z') {
        return c - 'a' + 26;
    }
    if (c >= '0' && c <= '9') {
        return c - '0' + 52;
    }
    if (c == '+') {
        return 62;
    }
    if (c == '/') {
        return 63;
    }
    return -1;
}

int iot_base64_encode(const void *input, iot_size_t input_len,
                      char *output, iot_size_t output_size,
                      iot_size_t *written)
{
    const uint8_t *src = (const uint8_t *)input;
    iot_size_t i, out = 0;
    iot_size_t needed = ((input_len + 2u) / 3u) * 4u;

    if (input == NULL || output == NULL) {
        return -1;
    }
    if (output_size < needed + 1u) {
        return -1;
    }

    for (i = 0; i < input_len; i += 3) {
        uint32_t v = ((uint32_t)src[i]) << 16;
        int remain = (int)(input_len - i);

        if (remain >= 2) {
            v |= ((uint32_t)src[i + 1]) << 8;
        }
        if (remain >= 3) {
            v |= (uint32_t)src[i + 2];
        }

        output[out++] = b64_alphabet[(v >> 18) & 0x3F];
        output[out++] = b64_alphabet[(v >> 12) & 0x3F];
        output[out++] = (remain >= 2) ? b64_alphabet[(v >> 6) & 0x3F] : '=';
        output[out++] = (remain >= 3) ? b64_alphabet[v & 0x3F]        : '=';
    }

    output[out] = '\0';
    if (written != NULL) {
        *written = out;
    }
    return IOT_OK;
}

int iot_base64_decode(const char *input, iot_size_t input_len,
                      void *output, iot_size_t output_size,
                      iot_size_t *written)
{
    uint8_t *dst = (uint8_t *)output;
    iot_size_t i, out = 0;
    uint32_t acc = 0;
    int nbits = 0;

    if (input == NULL || output == NULL) {
        return -1;
    }

    for (i = 0; i < input_len; i++) {
        char c = input[i];
        int v;

        if (c == '=' || c == '\n' || c == '\r') {
            if (c == '=') {
                /* 忽略填充，循环结束后统一收尾 */
                break;
            }
            continue;
        }
        v = b64_value(c);
        if (v < 0) {
            return -1;
        }
        acc = (acc << 6) | (uint32_t)v;
        nbits += 6;
        if (nbits >= 8) {
            nbits -= 8;
            if (out >= output_size) {
                return -1;
            }
            dst[out++] = (uint8_t)((acc >> nbits) & 0xFFu);
        }
    }

    if (written != NULL) {
        *written = out;
    }
    return IOT_OK;
}