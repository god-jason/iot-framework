/*
 * MD5 (RFC 1321) 摘要算法实现，内部按小端序处理 32 位字。
 *
 * 职责：
 *   提供流式（init/update/final）与一次性（compute）两种摘要接口。
 *
 * 数据流：
 *   输入字节逐字节写入上下文内的 64 字节分组缓冲，攒满一块立即压缩；
 *   final 阶段追加 0x80、零填充与 64 位消息比特长度并输出 16 字节摘要。
 *
 * 依赖与边界：
 *   - 仅依赖 <string.h>，无动态内存，上下文由调用者分配；
 *   - 上下文 buffer 固定 64 字节，update 可任意分片调用；
 *   - final 的临时 pad 缓冲为 72 字节，最大使用量恰为 72
 *     （当块内偏移为 56 时需补 64 字节填充 + 8 字节长度）。
 */
#include "md5.h"
#include <string.h>

/* 64 个轮常量：K[i] = floor(2^32 * |sin(i + 1)|) */
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

/* 每步循环左移位数，按 4 轮 x 16 步排列 */
static const uint8_t S[64] = {
    7,12,17,22,7,12,17,22,7,12,17,22,7,12,17,22,
    5,9,14,20,5,9,14,20,5,9,14,20,5,9,14,20,
    4,11,16,23,4,11,16,23,4,11,16,23,4,11,16,23,
    6,10,15,21,6,10,15,21,6,10,15,21,6,10,15,21
};

/* 32 位循环左移 n 位；n 取值范围 4..23，不会出现移 0 位的情况 */
#define ROTL(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

/* 按小端序读取 32 位字（MD5 的消息字与输出均为小端） */
static uint32_t load32_le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* 按小端序写回 32 位字 */
static void store32_le(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v);
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

/*
 * 对一个 64 字节分组执行 MD5 压缩，并把结果累加进 ctx->state。
 * 参数：ctx 摘要上下文；block 指向完整的 64 字节输入块。
 * 说明：a/b/c/d 为四个工作寄存器，f 为当前轮函数，g 为参与运算的
 *       消息字下标；64 步结束后与旧状态逐字相加（MD5 的累加结构）。
 */
static void md5_transform(md5_t *ctx, const uint8_t block[64])
{
    uint32_t m[16];
    uint32_t a = ctx->state[0];
    uint32_t b = ctx->state[1];
    uint32_t c = ctx->state[2];
    uint32_t d = ctx->state[3];
    uint32_t f, g;
    int i;

    for (i = 0; i < 16; i++) {
        /* 把 64 字节分组拆成 16 个 32 位小端消息字 */
        m[i] = load32_le(block + i * 4);
    }

    for (i = 0; i < 64; i++) {
        /* 第 1 轮：F(b,c,d)，消息字顺序 0..15 */
        if (i < 16) {
            f = (b & c) | (~b & d);
            g = i;
        } else if (i < 32) {
            /* 第 2 轮：G(b,c,d)，消息字顺序 (5*i+1) mod 16 */
            f = (d & b) | (~d & c);
            g = (5 * i + 1) % 16;
        } else if (i < 48) {
            /* 第 3 轮：H(b,c,d)，消息字顺序 (3*i+5) mod 16 */
            f = b ^ c ^ d;
            g = (3 * i + 5) % 16;
        } else {
            /* 第 4 轮：I(b,c,d)，消息字顺序 (7*i) mod 16 */
            f = c ^ (b | ~d);
            g = (7 * i) % 16;
        }
        /* 累加：当前工作变量 a + 轮函数 f + 常量 K + 消息字 m[g] */
        f = f + a + K[i] + m[g];
        /* 寄存器轮转：b 更新为 b + 循环左移(f, S[i]) */
        a = d;
        d = c;
        c = b;
        b = b + ROTL(f, S[i]);
    }

    /* 将本分组结果累加回链式状态 */
    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
}

/*
 * 初始化 MD5 上下文。
 * 参数：ctx 待初始化的上下文；为 NULL 时直接返回。
 * 说明：写入 RFC 1321 规定的 4 个初始状态字，并把比特计数清零。
 */
void md5_init(md5_t *ctx)
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

/*
 * 追加输入数据（可多次、分片调用）。
 * 参数：ctx 上下文；data 输入数据；len 字节数。
 * 说明：ctx 或 data 为 NULL、len 为 0 时静默返回；每写满 64 字节
 *       立刻调用压缩函数，因此 ctx->buffer 中始终只保留未满一块的数据。
 */
void md5_update(md5_t *ctx, const void *data, uint32_t len)
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
            md5_transform(ctx, ctx->buffer);
        }
    }
}

/*
 * 结束计算并输出 16 字节摘要。
 * 参数：ctx 上下文；digest 输出缓冲，至少 16 字节。
 * 说明：
 *   - 先记录原始比特长度（长度字段必须使用填充前的值）；
 *   - 按 idx < 56 补到 56 字节、否则补到 120 字节，
 *     末尾 8 字节以小端写入比特长度；
 *   - 调用后 ctx 状态被破坏，如需复用必须重新 init。
 */
void md5_final(md5_t *ctx, uint8_t digest[16])
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
    pad[0] = 0x80;   /* MD5 填充起始标记 */
    /* 末尾 8 字节写入 64 位比特长度（小端） */
    for (uint32_t i = 0; i < 8; i++) {
        pad[pad_len + i] = (uint8_t)(bits >> (i * 8));
    }

    /* 把填充数据当作普通输入继续计算，凑满最后的整块 */
    md5_update(ctx, pad, pad_len + 8);

    /* 4 个状态字按小端序输出，得到 16 字节摘要 */
    store32_le(digest,       ctx->state[0]);
    store32_le(digest + 4,   ctx->state[1]);
    store32_le(digest + 8,   ctx->state[2]);
    store32_le(digest + 12,  ctx->state[3]);
}

/*
 * 一次性计算 MD5 摘要。
 * 参数：data 输入数据；len 字节数；digest 输出缓冲（16 字节）。
 * 返回：无；内部使用栈上上下文，不保存任何状态。
 */
void md5_compute(const void *data, uint32_t len, uint8_t digest[16])
{
    md5_t ctx;
    md5_init(&ctx);
    md5_update(&ctx, data, len);
    md5_final(&ctx, digest);
}