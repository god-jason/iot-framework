/*
 * master_auth.c - IoT-DMP 平台 MQTT 鉴权签名
 *
 * Password = HMAC-MD5(model_id + "/" + device_id + "/" + model_secret,
 *                      model_secret)
 * 输出 32 字符 hex 小写。
 */

#include "master.h"

#include "md5.h"

#include <string.h>

#define HMAC_BLOCK_SIZE 64U

static void hex_lower(const uint8_t *digest, uint32_t len, char *out)
{
    static const char hex[] = "0123456789abcdef";
    uint32_t i;

    for (i = 0U; i < len; i++) {
        out[i * 2U] = hex[(digest[i] >> 4) & 0x0FU];
        out[i * 2U + 1U] = hex[digest[i] & 0x0FU];
    }
    out[len * 2U] = '\0';
}

int master_auth_password(const char *model_id, const char *device_id,
                         const char *model_secret, char out[33])
{
    uint8_t key[HMAC_BLOCK_SIZE];
    uint8_t ipad[HMAC_BLOCK_SIZE];
    uint8_t opad[HMAC_BLOCK_SIZE];
    uint8_t inner[16];
    uint8_t digest[16];
    md5_t ctx;
    size_t klen;
    uint32_t i;

    if (model_id == NULL || device_id == NULL || model_secret == NULL ||
        out == NULL || model_id[0] == '\0' || device_id[0] == '\0' ||
        model_secret[0] == '\0') {
        return -1;
    }

    /* 密钥超过 64B 时先做一次 MD5，再右侧补 0 */
    memset(key, 0, sizeof(key));
    klen = strlen(model_secret);
    if (klen > HMAC_BLOCK_SIZE) {
        md5_compute(model_secret, (uint32_t)klen, key);
    } else {
        memcpy(key, model_secret, klen);
    }

    for (i = 0U; i < HMAC_BLOCK_SIZE; i++) {
        ipad[i] = (uint8_t)(key[i] ^ 0x36U);
        opad[i] = (uint8_t)(key[i] ^ 0x5CU);
    }

    /* inner = MD5(ipad || model_id || "/" || device_id || "/" || secret) */
    md5_init(&ctx);
    md5_update(&ctx, ipad, sizeof(ipad));
    md5_update(&ctx, model_id, (uint32_t)strlen(model_id));
    md5_update(&ctx, "/", 1U);
    md5_update(&ctx, device_id, (uint32_t)strlen(device_id));
    md5_update(&ctx, "/", 1U);
    md5_update(&ctx, model_secret, (uint32_t)strlen(model_secret));
    md5_final(&ctx, inner);

    /* digest = MD5(opad || inner) */
    md5_init(&ctx);
    md5_update(&ctx, opad, sizeof(opad));
    md5_update(&ctx, inner, sizeof(inner));
    md5_final(&ctx, digest);

    hex_lower(digest, sizeof(digest), out);
    return 0;
}
