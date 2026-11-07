/* MD5 (RFC 1321) 标准实现，小端内部序。 */
#include "hash_md5.h"
#include <string.h>

static const uint32_t K[64] = {
    0xd76aa478u,0xe8c7b756u,0x242070dbu,0xc1bdceeeu,
    0xf57c0fafu,0x4787c62au,0xa8304613u,0xfd469501u,
    0x698098d8u,0x8b44f7afu,0xffff5bb1u,0x895cd7beu,
    0x6b901122u,0xfd987193u,0xa679438eu,0x49b40821u,
    0xf61e2562u,0xc040b340u,0x265e5a51u,0xe9b6c7aau,
    0xd62f105du,0x02441453u,0xd8a1e681u,0xe7d3fbc8u,
    0x21e1cde6u,0xc33707d6u,0xf4d50d87u,0x455a14edu,
    0xa9e3e905u,0xfcefa3f8u,0x676f02d9u,0x8d2a4c8au,
    0xfffa3942u,0x8771f681u,0x6d9d6122u,0xfde5380cu,
    0xa4beea44u,0x4bdecfa9u,0xf6bb4b60u,0xbebfbc70u,
    0x289b7ec6u,0xeaa127fau,0xd4ef3085u,0x04881d05u,
    0xd9d4d039u,0xe6db99e5u,0x1fa27cf8u,0xc4ac5665u,
    0xf4292244u,0x432aff97u,0xab9423a7u,0xfc93a039u,
    0x655b59c3u,0x8f0ccc92u,0xffeff47du,0x85845dd1u,
    0x6fa87e4fu,0xfe2ce6e0u,0xa3014314u,0x4e0811a1u,
    0xf7537e82u,0xbd3af235u,0x2ad7d2bbu,0xeb86d391u
};

static const uint8_t S[64] = {
    7,12,17,22,7,12,17,22,7,12,17,22,7,12,17,22,
    5,9,14,20,5,9,14,20,5,9,14,20,5,9,14,20,
    4,11,16,23,4,11,16,23,4,11,16,23,4,11,16,23,
    6,10,15,21,6,10,15,21,6,10,15,21,6,10,15,21
};

#define ROTL(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

static uint32_t load32_le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void store32_le(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v);
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void hash_md5_transform(hash_md5_t *ctx, const uint8_t block[64])
{
    uint32_t m[16];
    uint32_t a = ctx->state[0];
    uint32_t b = ctx->state[1];
    uint32_t c = ctx->state[2];
    uint32_t d = ctx->state[3];
    uint32_t f, g;
    int i;

    for (i = 0; i < 16; i++) {
        m[i] = load32_le(block + i * 4);
    }

    for (i = 0; i < 64; i++) {
        if (i < 16) {
            f = (b & c) | (~b & d);
            g = i;
        } else if (i < 32) {
            f = (d & b) | (~d & c);
            g = (5 * i + 1) % 16;
        } else if (i < 48) {
            f = b ^ c ^ d;
            g = (3 * i + 5) % 16;
        } else {
            f = c ^ (b | ~d);
            g = (7 * i) % 16;
        }
        f = f + a + K[i] + m[g];
        a = d;
        d = c;
        c = b;
        b = b + ROTL(f, S[i]);
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
}

void hash_md5_init(hash_md5_t *ctx)
{
    if (ctx == NULL) {
        return;
    }
    ctx->state[0] = 0x67452301u;
    ctx->state[1] = 0xefcdab89u;
    ctx->state[2] = 0x98badcfeu;
    ctx->state[3] = 0x10325476u;
    ctx->bit_count = 0;
}

void hash_md5_update(hash_md5_t *ctx, const void *data, uint32_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t i;

    if (ctx == NULL || data == NULL || len == 0) {
        return;
    }
    for (i = 0; i < len; i++) {
        uint32_t idx = (uint32_t)((ctx->bit_count >> 3) & 0x3F);
        ctx->buffer[idx] = p[i];
        ctx->bit_count += 8;
        if (((ctx->bit_count >> 3) & 0x3F) == 0) {
            hash_md5_transform(ctx, ctx->buffer);
        }
    }
}

void hash_md5_final(hash_md5_t *ctx, uint8_t digest[16])
{
    uint64_t bits;
    uint32_t idx;
    uint8_t pad[72];
    uint32_t pad_len;

    if (ctx == NULL || digest == NULL) {
        return;
    }
    bits = ctx->bit_count;
    idx = (uint32_t)((bits >> 3) & 0x3F);
    pad_len = (idx < 56) ? (56 - idx) : (120 - idx);

    memset(pad, 0, sizeof(pad));
    pad[0] = 0x80;
    for (uint32_t i = 0; i < 8; i++) {
        pad[pad_len + i] = (uint8_t)(bits >> (i * 8));
    }

    hash_md5_update(ctx, pad, pad_len + 8);

    store32_le(digest,       ctx->state[0]);
    store32_le(digest + 4,   ctx->state[1]);
    store32_le(digest + 8,   ctx->state[2]);
    store32_le(digest + 12,  ctx->state[3]);
}

void hash_md5_compute(const void *data, uint32_t len, uint8_t digest[16])
{
    hash_md5_t ctx;
    hash_md5_init(&ctx);
    hash_md5_update(&ctx, data, len);
    hash_md5_final(&ctx, digest);
}