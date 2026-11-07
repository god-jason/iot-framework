#include "iot_hex.h"

static char hex_nibble(uint8_t v, bool uppercase)
{
    if (v < 10) {
        return (char)('0' + v);
    }
    return (char)((uppercase ? 'A' : 'a') + (v - 10));
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

int iot_hex_encode(const void *input, iot_size_t input_len,
                   char *output, iot_size_t output_size,
                   bool uppercase)
{
    const uint8_t *src = (const uint8_t *)input;
    iot_size_t i;

    if (input == NULL || output == NULL) {
        return -1;
    }
    if (output_size < (input_len * 2u + 1u)) {
        return -1;
    }
    for (i = 0; i < input_len; i++) {
        output[i * 2u]     = hex_nibble((uint8_t)(src[i] >> 4), uppercase);
        output[i * 2u + 1] = hex_nibble((uint8_t)(src[i] & 0x0F), uppercase);
    }
    output[input_len * 2u] = '\0';
    return IOT_OK;
}

int iot_hex_decode(const char *input, iot_size_t input_len,
                   void *output, iot_size_t output_size,
                   iot_size_t *written)
{
    uint8_t *dst = (uint8_t *)output;
    iot_size_t i;

    if (input == NULL || output == NULL) {
        return -1;
    }
    if ((input_len & 1u) != 0) {
        return -1;
    }
    if (output_size < (input_len / 2u)) {
        return -1;
    }
    for (i = 0; i < input_len / 2u; i++) {
        int hi = hex_value(input[i * 2u]);
        int lo = hex_value(input[i * 2u + 1]);
        if (hi < 0 || lo < 0) {
            return -1;
        }
        dst[i] = (uint8_t)((hi << 4) | lo);
    }
    if (written != NULL) {
        *written = input_len / 2u;
    }
    return IOT_OK;
}