/*
 * storage_test.c - 存储注册表与 KV 测试（对应 storage.c / storage_kv.c）
 *
 * 覆盖：后端注册/注销、读写擦除分发、未实现回调、KV 增删改查与类型校验。
 */

#include "test.h"
#include "storage.h"
#include "storage_kv.h"

typedef struct {
    uint8_t mem[64];
    int erase_calls;
} flash_sim_t;

static int sim_init(void *ctx)
{
    memset(((flash_sim_t *)ctx)->mem, 0xFF, 64U);
    return 0;
}

static int sim_read(void *ctx, uint32_t addr, void *data, uint32_t len)
{
    flash_sim_t *s = (flash_sim_t *)ctx;
    if (addr + len > sizeof(s->mem)) {
        return -1;
    }
    memcpy(data, s->mem + addr, len);
    return 0;
}

static int sim_write(void *ctx, uint32_t addr, const void *data, uint32_t len)
{
    flash_sim_t *s = (flash_sim_t *)ctx;
    if (addr + len > sizeof(s->mem)) {
        return -1;
    }
    memcpy(s->mem + addr, data, len);
    return 0;
}

static int sim_erase(void *ctx, uint32_t addr, uint32_t len)
{
    flash_sim_t *s = (flash_sim_t *)ctx;
    if (addr + len > sizeof(s->mem)) {
        return -1;
    }
    memset(s->mem + addr, 0xFF, len);
    s->erase_calls++;
    return 0;
}

void ut_storage(void)
{
    flash_sim_t flash;
    storage_ops_t ops;
    uint8_t buf[8];
    const uint8_t payload[4] = { 1U, 2U, 3U, 4U };

    memset(&flash, 0, sizeof(flash));
    ops.init = sim_init;
    ops.read = sim_read;
    ops.write = sim_write;
    ops.erase = sim_erase;
    ops.ctx = &flash;

    tf_begin("storage/注册与注销");

    TF_CHECK_INT(storage_register(NULL, &ops), -1);
    TF_CHECK_INT(storage_register("flash", NULL), -1);
    TF_CHECK_INT(storage_register("flash", &ops), 0);
    TF_CHECK_INT(storage_unregister("nope"), -1);
    TF_CHECK_INT(storage_unregister(NULL), -1);

    tf_end();

    tf_begin("storage/读写擦除");

    TF_CHECK_INT(storage_write("flash", 0U, payload, 4U), 0);
    TF_CHECK_INT(flash.mem[0], 1U);
    TF_CHECK_INT(flash.mem[3], 4U);

    memset(buf, 0, sizeof(buf));
    TF_CHECK_INT(storage_read("flash", 0U, buf, 4U), 0);
    TF_CHECK_MEM(buf, payload, 4U);

    TF_CHECK_INT(storage_erase("flash", 0U, 4U), 0);
    TF_CHECK_INT(flash.erase_calls, 1);
    TF_CHECK_INT(flash.mem[0], 0xFFU);

    TF_CHECK_INT(storage_read("nope", 0U, buf, 1U), -1);
    TF_CHECK_INT(storage_write("nope", 0U, buf, 1U), -1);
    TF_CHECK_INT(storage_erase("nope", 0U, 1U), -1);
    TF_CHECK_INT(storage_read("flash", 60U, buf, 8U), -1);   /* 越界 */

    /* 未实现回调的后端 */
    {
        storage_ops_t partial;

        memset(&partial, 0, sizeof(partial));
        partial.ctx = &flash;
        TF_CHECK_INT(storage_register("partial", &partial), 0);
        TF_CHECK_INT(storage_read("partial", 0U, buf, 1U), -1);
        TF_CHECK_INT(storage_write("partial", 0U, buf, 1U), -1);
        TF_CHECK_INT(storage_erase("partial", 0U, 1U), -1);
        TF_CHECK_INT(storage_unregister("partial"), 0);
    }

    tf_end();

    tf_begin("storage/KV 读写");

    TF_CHECK_INT(storage_kv_init(), 0);

    {
        int32_t i32 = 42;
        float f = 1.5f;
        const char *s = "hello";
        uint8_t blob[3] = { 7U, 8U, 9U };
        int32_t out_i = 0;
        float out_f = 0.0f;
        char out_s[16];
        uint8_t out_b[8];
        uint32_t actual = 0;

        TF_CHECK_INT(storage_kv_set("i", STORAGE_KV_INT, &i32, sizeof(i32)), 0);
        TF_CHECK_INT(storage_kv_get("i", STORAGE_KV_INT, &out_i, sizeof(out_i),
                                    &actual), 0);
        TF_CHECK_INT(out_i, 42);
        TF_CHECK_INT(actual, sizeof(i32));

        TF_CHECK_INT(storage_kv_set("f", STORAGE_KV_FLOAT, &f, sizeof(f)), 0);
        TF_CHECK_INT(storage_kv_get("f", STORAGE_KV_FLOAT, &out_f,
                                    sizeof(out_f), NULL), 0);
        TF_CHECK_NEAR(out_f, 1.5f, 1e-6);

        TF_CHECK_INT(storage_kv_set("s", STORAGE_KV_STRING, s, 6U), 0);
        memset(out_s, 0, sizeof(out_s));
        TF_CHECK_INT(storage_kv_get("s", STORAGE_KV_STRING, out_s,
                                    sizeof(out_s), NULL), 0);
        TF_CHECK_STR(out_s, "hello");

        TF_CHECK_INT(storage_kv_set("b", STORAGE_KV_BLOB, blob, 3U), 0);
        TF_CHECK_INT(storage_kv_get("b", STORAGE_KV_BLOB, out_b, sizeof(out_b),
                                    NULL), 0);
        TF_CHECK_MEM(out_b, blob, 3U);

        /* 类型不匹配 */
        TF_CHECK_INT(storage_kv_get("i", STORAGE_KV_FLOAT, &out_f,
                                    sizeof(out_f), NULL), -1);
        /* 键不存在 */
        TF_CHECK_INT(storage_kv_get("nope", STORAGE_KV_INT, &out_i,
                                    sizeof(out_i), NULL), -1);
        /* 非法参数 */
        TF_CHECK_INT(storage_kv_set(NULL, STORAGE_KV_INT, &i32, 4U), -1);
        TF_CHECK_INT(storage_kv_set("x", STORAGE_KV_INT, NULL, 4U), -1);
        TF_CHECK_INT(storage_kv_set("x", STORAGE_KV_INT, &i32, 0U), 0);
    }

    tf_end();

    tf_begin("storage/KV 覆盖与删除");

    {
        int32_t v1 = 1;
        int32_t v2 = 2;
        int32_t out = 0;

        TF_CHECK_INT(storage_kv_set("k", STORAGE_KV_INT, &v1, sizeof(v1)), 0);
        TF_CHECK_INT(storage_kv_set("k", STORAGE_KV_INT, &v2, sizeof(v2)), 0);
        TF_CHECK_INT(storage_kv_get("k", STORAGE_KV_INT, &out, sizeof(out),
                                    NULL), 0);
        TF_CHECK_INT(out, 2);

        TF_CHECK_INT(storage_kv_delete("k"), 0);
        TF_CHECK_INT(storage_kv_delete("k"), -1);
        TF_CHECK_INT(storage_kv_get("k", STORAGE_KV_INT, &out, sizeof(out),
                                    NULL), -1);
        TF_CHECK_INT(storage_kv_clear(), 0);
        TF_CHECK_INT(storage_kv_commit(), 0);
    }

    tf_end();
}
