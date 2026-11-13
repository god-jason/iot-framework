/*
 * map_test.c - 跳表键值映射回归测试
 */

#include "test.h"
#include "map.h"

#include <stdio.h>

void ut_map(void)
{
    map_t m;
    int values[64];
    char key[16];
    void *out = NULL;
    uint32_t i;

    tf_begin("map/跳表键值映射");

    /* 初始化 / 释放 */
    TF_CHECK_INT(map_init(NULL), -1);
    TF_CHECK_INT(map_init(&m), 0);
    TF_CHECK_INT((int)map_size(&m), 0);

    /* 大量插入（key 由 map 复制，可复用可变缓冲） */
    for (i = 0U; i < 64U; i++) {
        (void)snprintf(key, sizeof(key), "k%u", (unsigned)i);
        values[i] = (int)i;
        TF_CHECK(map_put(&m, key, &values[i]));
    }
    TF_CHECK_INT((int)map_size(&m), 64);

    /* 查找全部键 */
    for (i = 0U; i < 64U; i++) {
        (void)snprintf(key, sizeof(key), "k%u", (unsigned)i);
        TF_CHECK(map_get(&m, key, &out) && out == &values[i]);
    }
    TF_CHECK(!map_get(&m, "k64", &out));
    TF_CHECK(!map_contains(&m, "k64"));
    TF_CHECK(map_get_ptr(&m, "k5") == &values[5]);

    /* 更新已有键 */
    TF_CHECK(map_put(&m, "k10", &values[0]));
    TF_CHECK(map_get(&m, "k10", &out) && out == &values[0]);
    TF_CHECK_INT((int)map_size(&m), 64);

    /* 删除 */
    TF_CHECK(map_remove(&m, "k0"));
    TF_CHECK(!map_remove(&m, "k0"));
    TF_CHECK(!map_contains(&m, "k0"));
    TF_CHECK_INT((int)map_size(&m), 63);

    /* 值为 NULL 也能区分存在性 */
    TF_CHECK(map_put(&m, "null", NULL));
    out = &values[0];
    TF_CHECK(map_get(&m, "null", &out) && out == NULL);

    /* 非法参数 */
    TF_CHECK(!map_put(&m, "", &values[0]));
    TF_CHECK(!map_put(&m, NULL, &values[0]));
    TF_CHECK(!map_get(NULL, "k1", &out));
    TF_CHECK_INT((int)map_size(NULL), 0);
    TF_CHECK(map_at(NULL, 0U) == NULL);

    /* 下标访问（按 key 升序） */
    TF_CHECK(map_at(&m, 0U) != NULL);
    TF_CHECK(map_at(&m, map_size(&m)) == NULL);

    /* 清空后可继续使用 */
    map_clear(&m);
    TF_CHECK_INT((int)map_size(&m), 0);
    TF_CHECK(map_put(&m, "again", &values[0]));
    TF_CHECK(map_contains(&m, "again"));

    /* 释放后不再可用 */
    map_deinit(&m);
    TF_CHECK_INT((int)map_size(&m), 0);
    TF_CHECK(!map_put(&m, "x", &values[0]));

    tf_end();
}
