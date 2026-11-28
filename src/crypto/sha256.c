/*
 * SHA-256 (FIPS 180-4) 摘要算法实现，内部按大端序处理 32 位字。
 *
 * 职责：
 *   提供流式（init/update/final）与一次性（compute）两种摘要接口，
 *   输出 32 字节（256 位）摘要，适用于固件完整性校验等场景。
 *
 * 数据流：
 *   输入字节逐字节写入 64 字节分组缓冲，攒满一块即做消息扩展并压缩；
 *   final 阶段追加 0x80、零填充与 64 位大端比特长度后输出 8 个状态字。
 *
 * 依赖与边界：
 *   - 仅依赖 <string.h>，无动态内存，上下文由调用者分配；
 *   - 压缩过程需要 64 个 32 位消息字（w[64]，约 256 字节栈空间）；
 *   - final 的临时 pad 缓冲 72 字节，最大使用量恰为 72。
 */
#include "sha256.h"
#include <string.h>

/* 64 个轮常量：前 64 个素数立方根小数部分的前 32 位 */
static const uint32_t K[64] = {
    0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,
    0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
    0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,
    0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
    0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,
    0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
    0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,
    0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
    0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,
    0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
    0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,
    0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
    0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,
    0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
    0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,
    0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u
};

/* 32 位循环右移 n 位；n 固定为 2/6/7/11/13/17/18/19/22/25，不会移 0 位 */
#define ROTR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

/* 按大端序读取 32 位字（SHA-256 输入消息与输出摘要均为大端） */
static uint32_t load32_be(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  | ((uint32_t)p[3]);
}

/* 按大端序写回 32 位字 */
static void store32_be(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)(v);
}

/*
 * 对一个 64 字节分组执行 SHA-256 压缩，并累加进 ctx->state。
 * 参数：ctx 摘要上下文；block 指向完整的 64 字节输入块（大端）。
 * 说明：先把 16 个字扩展成 64 个消息字 w[64]，再用 8 个工作寄存器
 *       a..h 执行 64 轮，最后与旧状态逐字相加。
 */
static void sha256_transform(sha256_t *ctx, const uint8_t block[64])
{
    uint32_t w[64];
    uint32_t a, b, c, d, e, f, g, h;
    int i;

    for (i = 0; i < 16; i++) {
        /* 前 16 个字直接来自输入块 */
        w[i] = load32_be(block + i * 4);
    }
    for (i = 16; i < 64; i++) {
        /* 消息扩展：w[i] = σ1(w[i-2]) + w[i-7] + σ0(w[i-15]) + w[i-16] */
        uint32_t s0 = ROTR(w[i - 15], 7) ^ ROTR(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = ROTR(w[i - 2], 17) ^ ROTR(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    /* 8 个工作寄存器从当前链式状态装载 */
    a = ctx->state[0]; b = ctx->state[1]; c = ctx->state[2]; d = ctx->state[3];
    e = ctx->state[4]; f = ctx->state[5]; g = ctx->state[6]; h = ctx->state[7];

    for (i = 0; i < 64; i++) {
        /* Σ1(e) 与选择函数 Ch(e,f,g) */
        uint32_t s1 = ROTR(e, 6) ^ ROTR(e, 11) ^ ROTR(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t temp1 = h + s1 + ch + K[i] + w[i];
        /* Σ0(a) 与多数函数 Maj(a,b,c) */
        uint32_t s0 = ROTR(a, 2) ^ ROTR(a, 13) ^ ROTR(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = s0 + maj;

        /* 寄存器整体右移一位完成轮变换 */
        h = g; g = f; f = e; e = d + temp1;
        d = c; c = b; b = a; a = temp1 + temp2;
    }

    /* 把本分组结果累加回链式状态 */
    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c; ctx->state[3] += d;
    ctx->state[4] += e; ctx->state[5] += f; ctx->state[6] += g; ctx->state[7] += h;
}

/*
 * 初始化 SHA-256 上下文。
 * 参数：ctx 待初始化的上下文；为 NULL 时直接返回。
 * 说明：写入前 8 个素数平方根小数部分的前 32 位作为初始状态。
 */
void sha256_init(sha256_t *ctx)
{
    if (ctx == NULL) {
        return;
    }
    ctx->state[0] = 0x6a09e667u;
    ctx->state[1] = 0xbb67ae85u;
    ctx->state[2] = 0x3c6ef372u;
    ctx->state[3] = 0xa54ff53au;
    ctx->state[4] = 0x510e527fu;
    ctx->state[5] = 0x9b05688cu;
    ctx->state[6] = 0x1f83d9abu;
    ctx->state[7] = 0x5be0cd19u;
    ctx->bit_count = 0;
}

/*
 * 追加输入数据（可多次、分片调用）。
 * 参数：ctx 上下文；data 输入数据；len 字节数。
 * 说明：ctx/data 为 NULL 或 len 为 0 时静默返回；每写满 64 字节
 *       触发一次压缩，buffer 中只保留未满一块的残留数据。
 */
void sha256_update(sha256_t *ctx, const void *data, uint32_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t i;

    if (ctx == NULL || data == NULL || len == 0) {
        return;
    }
    for (i = 0; i < len; i++) {
        /* 当前块内偏移 = 已处理字节数 mod 64 */
        uint32_t idx = (uint32_t)((ctx->bit_count >> 3) & 0x3F);
        ctx->buffer[idx] = p[i];
        ctx->bit_count += 8;
        /* 偏移归零表示刚好写满 64 字节，触发一次压缩 */
        if (((ctx->bit_count >> 3) & 0x3F) == 0) {
            sha256_transform(ctx, ctx->buffer);
        }
    }
}

/*
 * 结束计算并输出 32 字节摘要。
 * 参数：ctx 上下文；digest 输出缓冲，至少 32 字节。
 * 说明：
 *   - 先记录填充前的原始比特长度；
 *   - idx < 56 时补到 56 字节，否则补到 120 字节，末尾 8 字节
 *     以大端写入比特长度；
 *   - 调用后上下文状态被破坏，需重新 init 才能复用。
 */
void sha256_final(sha256_t *ctx, uint8_t digest[32])
{
    uint64_t bits;
    uint32_t idx;
    uint8_t pad[72];
    uint32_t pad_len;

    if (ctx == NULL || digest == NULL) {
        return;
    }
    bits = ctx->bit_count;
    /* 块内当前字节偏移 */
    idx = (uint32_t)((bits >> 3) & 0x3F);
    /* 剩余空间不足 8 字节时需多补一整块：56-63 偏移对应补到 120 */
    pad_len = (idx < 56) ? (56 - idx) : (120 - idx);

    memset(pad, 0, sizeof(pad));
    pad[0] = 0x80;   /* SHA 填充起始标记 */
    /* 末尾 8 字节写入 64 位比特长度（大端） */
    for (int i = 0; i < 8; i++) {
        pad[pad_len + i] = (uint8_t)(bits >> (56 - i * 8));
    }

    /* 把填充数据当作普通输入继续计算，凑满最后的整块 */
    sha256_update(ctx, pad, pad_len + 8);

    /* 8 个状态字按大端序输出，得到 32 字节摘要 */
    for (int i = 0; i < 8; i++) {
        store32_be(digest + i * 4, ctx->state[i]);
    }
}

/*
 * 一次性计算 SHA-256 摘要。
 * 参数：data 输入数据；len 字节数；digest 输出缓冲（32 字节）。
 * 返回：无；内部使用栈上上下文，不保存任何状态。
 */
void sha256_compute(const void *data, uint32_t len, uint8_t digest[32])
{
    sha256_t ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, data, len);
    sha256_final(&ctx, digest);
}