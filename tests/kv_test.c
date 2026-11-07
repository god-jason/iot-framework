/* KV 存储测试：写入/读取/删除/覆盖、类型校验、截断、容量与清空。 */
#include "test_framework.h"
#include "storage_kv.h"

#include <string.h>

void ut_kv(void)
{
    int32_t in;
    int32_t out;
    uint32_t actual;
    uint8_t blob[32];
    uint8_t got[32];
    char key[16];
    char name[8];
    int i;

    memset(blob, 0x5A, sizeof(blob));

    tf_begin("kv/初始化与缺省查询");
    TF_CHECK_INT(storage_kv_init(), IOT_OK);
    out = 0;
    actual = 0;
    TF_CHECK_INT(storage_kv_get("absent", STORAGE_KV_INT, &out, sizeof(out), &actual), -1);
    TF_CHECK_INT(storage_kv_delete("absent"), -1);
    TF_CHECK_INT(storage_kv_commit(), IOT_OK);
    tf_end();

    tf_begin("kv/整数写入与读取");
    in = 123456;
    TF_CHECK_INT(storage_kv_set("counter", STORAGE_KV_INT, &in, sizeof(in)), IOT_OK);
    out = 0;
    actual = 0;
    TF_CHECK_INT(storage_kv_get("counter", STORAGE_KV_INT, &out, sizeof(out), &actual), IOT_OK);
    TF_CHECK_INT(out, 123456);
    TF_CHECK_INT(actual, sizeof(in));

    /* actual_size 可为 NULL */
    out = 0;
    TF_CHECK_INT(storage_kv_get("counter", STORAGE_KV_INT, &out, sizeof(out), NULL), IOT_OK);
    TF_CHECK_INT(out, 123456);
    tf_end();

    tf_begin("kv/类型不匹配被拒绝");
    out = 0;
    TF_CHECK_INT(storage_kv_get("counter", STORAGE_KV_FLOAT, &out, sizeof(out), &actual), -1);
    TF_CHECK_INT(storage_kv_get("counter", STORAGE_KV_STRING, &out, sizeof(out), &actual), -1);
    tf_end();

    tf_begin("kv/同键覆盖写");
    {
        const char *hello = "hello";
        char str[16];

        TF_CHECK_INT(storage_kv_set("counter", STORAGE_KV_STRING, hello, 6), IOT_OK);
        memset(str, 0, sizeof(str));
        actual = 0;
        TF_CHECK_INT(storage_kv_get("counter", STORAGE_KV_STRING, str, sizeof(str), &actual), IOT_OK);
        TF_CHECK_STR(str, "hello");
        TF_CHECK_INT(actual, 6);
        /* 旧类型不再可读 */
        TF_CHECK_INT(storage_kv_get("counter", STORAGE_KV_INT, &out, sizeof(out), &actual), -1);
    }
    tf_end();

    tf_begin("kv/写入参数校验");
    in = 1;
    TF_CHECK_INT(storage_kv_set(NULL, STORAGE_KV_INT, &in, sizeof(in)), -1);
    TF_CHECK_INT(storage_kv_set("k", STORAGE_KV_INT, NULL, sizeof(in)), -1);
    TF_CHECK_INT(storage_kv_set("k", STORAGE_KV_BLOB, blob, 33), -1);   /* 超过 32 字节 */
    TF_CHECK_INT(storage_kv_set("k", STORAGE_KV_BLOB, blob, 32), IOT_OK);
    actual = 0;
    TF_CHECK_INT(storage_kv_get("k", STORAGE_KV_BLOB, got, sizeof(got), &actual), IOT_OK);
    TF_CHECK_INT(actual, 32);
    TF_CHECK_MEM(got, blob, 32);
    tf_end();

    tf_begin("kv/零长度值");
    actual = 99;
    TF_CHECK_INT(storage_kv_set("empty", STORAGE_KV_BLOB, blob, 0), IOT_OK);
    TF_CHECK_INT(storage_kv_get("empty", STORAGE_KV_BLOB, got, sizeof(got), &actual), IOT_OK);
    TF_CHECK_INT(actual, 0);
    tf_end();

    tf_begin("kv/读取参数与缓冲区截断");
    TF_CHECK_INT(storage_kv_get(NULL, STORAGE_KV_INT, &out, sizeof(out), &actual), -1);
    TF_CHECK_INT(storage_kv_get("counter", STORAGE_KV_STRING, NULL, 16, &actual), -1);

    TF_CHECK_INT(storage_kv_set("trunc", STORAGE_KV_BLOB, blob, 8), IOT_OK);
    memset(got, 0, sizeof(got));
    actual = 0;
    TF_CHECK_INT(storage_kv_get("trunc", STORAGE_KV_BLOB, got, 4, &actual), IOT_OK);
    TF_CHECK_INT(actual, 8);                 /* 实际长度仍为完整长度 */
    TF_CHECK_MEM(got, blob, 4);              /* 只拷贝目标容量 */

    /* 目标容量为 0 时仍返回成功且不写入 */
    memset(got, 0, sizeof(got));
    actual = 0;
    TF_CHECK_INT(storage_kv_get("trunc", STORAGE_KV_BLOB, got, 0, &actual), IOT_OK);
    TF_CHECK_INT(actual, 8);
    TF_CHECK_INT(got[0], 0);
    tf_end();

    tf_begin("kv/删除与重建");
    TF_CHECK_INT(storage_kv_delete(NULL), -1);
    TF_CHECK_INT(storage_kv_delete("trunc"), IOT_OK);
    TF_CHECK_INT(storage_kv_get("trunc", STORAGE_KV_BLOB, got, sizeof(got), &actual), -1);
    TF_CHECK_INT(storage_kv_delete("trunc"), -1);

    in = 9;
    TF_CHECK_INT(storage_kv_set("trunc", STORAGE_KV_INT, &in, sizeof(in)), IOT_OK);
    out = 0;
    TF_CHECK_INT(storage_kv_get("trunc", STORAGE_KV_INT, &out, sizeof(out), &actual), IOT_OK);
    TF_CHECK_INT(out, 9);
    tf_end();

    tf_begin("kv/清空全部");
    TF_CHECK_INT(storage_kv_clear(), IOT_OK);
    TF_CHECK_INT(storage_kv_get("trunc", STORAGE_KV_INT, &out, sizeof(out), &actual), -1);
    TF_CHECK_INT(storage_kv_get("counter", STORAGE_KV_STRING, got, sizeof(got), &actual), -1);
    TF_CHECK_INT(storage_kv_commit(), IOT_OK);
    tf_end();

    tf_begin("kv/超长键被截断为 15 字符");
    TF_CHECK_INT(storage_kv_clear(), IOT_OK);
    in = 7;
    TF_CHECK_INT(storage_kv_set("0123456789abcdef", STORAGE_KV_INT, &in, sizeof(in)), IOT_OK);
    out = 0;
    TF_CHECK_INT(storage_kv_get("0123456789abcde", STORAGE_KV_INT, &out, sizeof(out), &actual), IOT_OK);
    TF_CHECK_INT(out, 7);
    /* 完整 16 字符键与存储的 15 字符键不相等 */
    TF_CHECK_INT(storage_kv_get("0123456789abcdef", STORAGE_KV_INT, &out, sizeof(out), &actual), -1);
    tf_end();

    tf_begin("kv/容量上限与槽位复用");
    TF_CHECK_INT(storage_kv_clear(), IOT_OK);
    for (i = 0; i < 32; i++) {
        in = i;
        snprintf(key, sizeof(key), "k%d", i);
        TF_CHECK_INT(storage_kv_set(key, STORAGE_KV_INT, &in, sizeof(in)), IOT_OK);
    }
    in = 32;
    TF_CHECK_INT(storage_kv_set("overflow", STORAGE_KV_INT, &in, sizeof(in)), -1);

    /* 删除一个键后空出的槽位可复用 */
    TF_CHECK_INT(storage_kv_delete("k5"), IOT_OK);
    TF_CHECK_INT(storage_kv_set("overflow", STORAGE_KV_INT, &in, sizeof(in)), IOT_OK);
    out = 0;
    TF_CHECK_INT(storage_kv_get("overflow", STORAGE_KV_INT, &out, sizeof(out), &actual), IOT_OK);
    TF_CHECK_INT(out, 32);
    tf_end();

    tf_begin("kv/重新初始化清空数据");
    TF_CHECK_INT(storage_kv_init(), IOT_OK);
    TF_CHECK_INT(storage_kv_get("overflow", STORAGE_KV_INT, &out, sizeof(out), &actual), -1);
    snprintf(name, sizeof(name), "k0");
    TF_CHECK_INT(storage_kv_get(name, STORAGE_KV_INT, &out, sizeof(out), &actual), -1);
    tf_end();
}
