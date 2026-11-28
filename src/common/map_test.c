/*
 * map_test.c - 跳表键值映射回归测试
 */

#include "test.h"
#include "map.h"
#include "common.h"

#include <stdio.h>

/* 统计遍历条目数 */
static bool map_count_cb(const map_item_t *item, void *ctx)
{
    int *count = (int *)ctx;

    (void)item;
    (*count)++;
    return true;
}

/* 访问一条后立即要求停止 */
static bool map_stop_cb(const map_item_t *item, void *ctx)
{
    int *count = (int *)ctx;

    (void)item;
    (*count)++;
    return false;
}

/* 记录遍历到的键（应按升序） */
typedef struct {
    char keys[4][8];
    int  count;
} map_walk_t;

static bool map_collect_cb(const map_item_t *item, void *ctx)
{
    map_walk_t *walk = (map_walk_t *)ctx;

    if (walk->count < 4) {
        (void)str_printf(walk->keys[walk->count], sizeof(walk->keys[0]),
                       "%s", item->key);
    }
    walk->count++;
    return true;
}

void ut_map(void)
{
    map_t m;
    map_iter_t it;
    map_item_t item;
    map_walk_t walk;
    int values[64];
    char key[16];
    void *out = NULL;
    int cnt = 0;
    uint32_t i;

    tf_begin("map/跳表键值映射");

    /* 初始化 / 释放 */
    TF_CHECK_INT(map_init(NULL), -1);
    TF_CHECK_INT(map_init(&m), 0);
    TF_CHECK_INT((int)map_size(&m), 0);

    /* 大量插入（key 由 map 复制，可复用可变缓冲） */
    for (i = 0U; i < 64U; i++) {
        (void)str_printf(key, sizeof(key), "k%u", (unsigned)i);
        values[i] = (int)i;
        TF_CHECK(map_put(&m, key, &values[i]));
    }
    TF_CHECK_INT((int)map_size(&m), 64);

    /* 查找全部键 */
    for (i = 0U; i < 64U; i++) {
        (void)str_printf(key, sizeof(key), "k%u", (unsigned)i);
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

    tf_begin("map/迭代与遍历");

    TF_CHECK_INT(map_init(&m), 0);

    /* 空表：迭代立即结束 */
    map_iter_begin(&m, &it);
    TF_CHECK(!map_iter_next(&it, &item));

    /* 插入 c,a,b 后应按 a,b,c 升序迭代 */
    (void)map_put(&m, "c", &values[2]);
    (void)map_put(&m, "a", &values[0]);
    (void)map_put(&m, "b", &values[1]);

    map_iter_begin(&m, &it);
    TF_CHECK(map_iter_next(&it, &item));
    TF_CHECK_STR(item.key, "a");
    TF_CHECK(item.value == &values[0]);
    TF_CHECK(map_iter_next(&it, &item));
    TF_CHECK_STR(item.key, "b");
    TF_CHECK(map_iter_next(&it, &item));
    TF_CHECK_STR(item.key, "c");
    TF_CHECK(!map_iter_next(&it, &item));

    /* foreach 全遍历：计数与顺序 */
    walk.count = 0;
    TF_CHECK(map_foreach(&m, map_collect_cb, &walk));
    TF_CHECK_INT(walk.count, 3);
    TF_CHECK_STR(walk.keys[0], "a");
    TF_CHECK_STR(walk.keys[2], "c");

    /* foreach 提前返回 false：只访问 1 条 */
    cnt = 0;
    TF_CHECK(!map_foreach(&m, map_stop_cb, &cnt));
    TF_CHECK_INT(cnt, 1);

    /* 单条目（value 为 NULL） */
    map_clear(&m);
    (void)map_put(&m, "only", NULL);
    map_iter_begin(&m, &it);
    TF_CHECK(map_iter_next(&it, &item));
    TF_CHECK_STR(item.key, "only");
    TF_CHECK(item.value == NULL);
    TF_CHECK(!map_iter_next(&it, &item));

    /* NULL / 未初始化安全 */
    map_iter_begin(NULL, &it);
    TF_CHECK(!map_iter_next(&it, &item));
    TF_CHECK(!map_foreach(NULL, map_count_cb, &cnt));
    TF_CHECK(!map_foreach(&m, NULL, NULL));

    map_deinit(&m);

    tf_end();
}
