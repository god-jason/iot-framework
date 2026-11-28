/*
 * vec_test.c - 变长数组回归测试
 */

#include "test.h"
#include "vec.h"

/* 判定偶数 */
static bool vec_pred_even(const void *elem, void *ctx)
{
    (void)ctx;
    return (*(const int *)elem % 2) == 0;
}

/* 判定字符串以 ctx 为前缀 */
static bool vec_pred_prefix(const void *elem, void *ctx)
{
    const char *s = (const char *)elem;
    const char *prefix = (const char *)ctx;

    return strncmp(s, prefix, strlen(prefix)) == 0;
}

static int vec_cmp_int(const void *a, const void *b)
{
    int va = *(const int *)a;
    int vb = *(const int *)b;

    return (va > vb) - (va < vb);
}

static int vec_cmp_str(const void *a, const void *b)
{
    return strcmp((const char *)a, (const char *)b);
}

void ut_vec(void)
{
    vec_t v;
    vec_t sv;
    vec_t zero;
    char buf[8];
    char prefix[4];
    int x = 0;
    int out = 0;
    uint32_t i;

    tf_begin("vec/变长数组");

    /* 初始化 / 参数校验 */
    TF_CHECK_INT(vec_init(NULL, 4U, 0U), -1);
    TF_CHECK_INT(vec_init(&v, 0U, 4U), -1);
    TF_CHECK_INT(vec_init(&v, sizeof(int), 0U), 0);
    TF_CHECK_INT((int)vec_size(&v), 0);
    TF_CHECK(vec_empty(&v));
    TF_CHECK(vec_at(&v, 0U) == NULL);

    /* push 触发多次扩容 */
    for (i = 0U; i < 100U; i++) {
        x = (int)i;
        TF_CHECK_INT(vec_push(&v, &x), 0);
    }
    TF_CHECK_INT((int)vec_size(&v), 100);
    TF_CHECK(vec_capacity(&v) >= 100U);
    for (i = 0U; i < 100U; i++) {
        TF_CHECK(*(int *)vec_at(&v, i) == (int)i);
    }

    /* pop */
    TF_CHECK_INT(vec_pop(&v, &out), 0);
    TF_CHECK_INT(out, 99);
    TF_CHECK_INT(vec_pop(&v, NULL), 0);
    TF_CHECK_INT((int)vec_size(&v), 98);
    TF_CHECK_INT(vec_pop(&(vec_t){0}, NULL), -1);

    /* insert */
    x = 7;
    TF_CHECK_INT(vec_insert(&v, 0U, &x), 0);
    TF_CHECK_INT(*(int *)vec_at(&v, 0U), 7);
    TF_CHECK_INT(*(int *)vec_at(&v, 1U), 0);
    TF_CHECK_INT(vec_insert(&v, vec_size(&v), &x), 0);
    TF_CHECK_INT(*(int *)vec_at(&v, vec_size(&v) - 1U), 7);
    TF_CHECK_INT(vec_insert(&v, vec_size(&v) + 1U, &x), -1);

    /* remove */
    TF_CHECK_INT(vec_remove(&v, 0U, &out), 0);
    TF_CHECK_INT(out, 7);
    TF_CHECK_INT(*(int *)vec_at(&v, 0U), 0);
    TF_CHECK_INT(vec_remove(&v, vec_size(&v), NULL), -1);

    /* resize：增长清零、缩小截断 */
    TF_CHECK_INT(vec_resize(&v, 110U), 0);
    TF_CHECK_INT((int)vec_size(&v), 110);
    for (i = 100U; i < 110U; i++) {
        TF_CHECK(*(int *)vec_at(&v, i) == 0);
    }
    TF_CHECK_INT(vec_resize(&v, 50U), 0);
    TF_CHECK_INT((int)vec_size(&v), 50);

    /* clear / reserve */
    vec_clear(&v);
    TF_CHECK(vec_empty(&v));
    TF_CHECK_INT(vec_reserve(&v, 1000U), 0);
    TF_CHECK(vec_capacity(&v) >= 1000U);
    TF_CHECK_INT((int)vec_size(&v), 0);
    TF_CHECK_INT(vec_reserve(&v, UINT32_MAX), -1);   /* 字节数溢出保护 */

    /* 释放 */
    vec_deinit(&v);
    TF_CHECK_INT((int)vec_size(&v), 0);
    TF_CHECK(vec_data(&v) == NULL);
    vec_deinit(&v);   /* 重复释放安全 */

    tf_end();

    tf_begin("vec/查找");

    TF_CHECK_INT(vec_init(&v, sizeof(int), 0U), 0);
    x = 1;
    TF_CHECK_INT(vec_find(&v, &x), -1);               /* 空表 */
    TF_CHECK_INT(vec_find(NULL, &x), -1);
    TF_CHECK_INT(vec_find(&v, NULL), -1);
    TF_CHECK_INT(vec_find_if(&v, NULL, NULL), -1);
    memset(&zero, 0, sizeof(zero));                   /* elem_size=0 不崩溃 */
    TF_CHECK_INT(vec_find(&zero, &x), -1);

    /* 3,1,3,2（含重复元素） */
    x = 3; (void)vec_push(&v, &x);
    x = 1; (void)vec_push(&v, &x);
    x = 3; (void)vec_push(&v, &x);
    x = 2; (void)vec_push(&v, &x);

    x = 1;
    TF_CHECK_INT(vec_find(&v, &x), 1);                /* 命中 */
    x = 9;
    TF_CHECK_INT(vec_find(&v, &x), -1);               /* 未命中 */
    x = 3;
    TF_CHECK_INT(vec_find_from(&v, 0U, &x), 0);
    TF_CHECK_INT(vec_find_from(&v, 1U, &x), 2);       /* 第二个 3 */
    TF_CHECK_INT(vec_find_from(&v, 3U, &x), -1);
    TF_CHECK_INT(vec_find_if(&v, vec_pred_even, NULL), 3);  /* 首个偶数 */

    /* 字符串前缀查找 */
    TF_CHECK_INT(vec_init(&sv, sizeof(buf), 0U), 0);
    memset(buf, 0, sizeof(buf));
    strcpy(buf, "apple"); (void)vec_push(&sv, buf);
    memset(buf, 0, sizeof(buf));
    strcpy(buf, "pear"); (void)vec_push(&sv, buf);
    memset(buf, 0, sizeof(buf));
    strcpy(buf, "apricot"); (void)vec_push(&sv, buf);
    strcpy(prefix, "ap");
    TF_CHECK_INT(vec_find_if(&sv, vec_pred_prefix, prefix), 0);
    strcpy(prefix, "zz");
    TF_CHECK_INT(vec_find_if(&sv, vec_pred_prefix, prefix), -1);
    memset(buf, 0, sizeof(buf));
    strcpy(buf, "pear");
    TF_CHECK_INT(vec_find(&sv, buf), 1);
    vec_deinit(&sv);

    tf_end();

    tf_begin("vec/排序与反转");

    TF_CHECK_INT(vec_init(&v, sizeof(int), 0U), 0);

    /* 空表：排序/反转安全 */
    vec_sort(&v, vec_cmp_int);
    vec_reverse(&v);
    TF_CHECK(vec_empty(&v));

    /* 单元素 + cmp 为 NULL */
    x = 42;
    TF_CHECK_INT(vec_push(&v, &x), 0);
    vec_sort(&v, vec_cmp_int);
    TF_CHECK_INT(*(int *)vec_at(&v, 0U), 42);
    vec_sort(&v, NULL);                               /* 不做任何事 */
    TF_CHECK_INT(*(int *)vec_at(&v, 0U), 42);

    /* 奇长度反转：1,2,3 -> 3,2,1 */
    vec_clear(&v);
    for (i = 1U; i <= 3U; i++) {
        x = (int)i;
        (void)vec_push(&v, &x);
    }
    vec_reverse(&v);
    TF_CHECK(*(int *)vec_at(&v, 0U) == 3 &&
             *(int *)vec_at(&v, 1U) == 2 &&
             *(int *)vec_at(&v, 2U) == 1);

    /* 偶长度反转：3,2,1,4 -> 4,1,2,3 */
    x = 4; (void)vec_push(&v, &x);
    vec_reverse(&v);
    TF_CHECK(*(int *)vec_at(&v, 0U) == 4 &&
             *(int *)vec_at(&v, 1U) == 1 &&
             *(int *)vec_at(&v, 2U) == 2 &&
             *(int *)vec_at(&v, 3U) == 3);

    /* int 升序：3,4,5,1,2 -> 1..5 */
    vec_clear(&v);
    for (i = 0U; i < 5U; i++) {
        x = (int)((i + 2U) % 5U) + 1;
        (void)vec_push(&v, &x);
    }
    vec_sort(&v, vec_cmp_int);
    TF_CHECK(*(int *)vec_at(&v, 0U) == 1 &&
             *(int *)vec_at(&v, 1U) == 2 &&
             *(int *)vec_at(&v, 2U) == 3 &&
             *(int *)vec_at(&v, 3U) == 4 &&
             *(int *)vec_at(&v, 4U) == 5);
    vec_deinit(&v);

    /* 字符串 strcmp 序 */
    TF_CHECK_INT(vec_init(&sv, sizeof(buf), 0U), 0);
    memset(buf, 0, sizeof(buf));
    strcpy(buf, "pear"); (void)vec_push(&sv, buf);
    memset(buf, 0, sizeof(buf));
    strcpy(buf, "apple"); (void)vec_push(&sv, buf);
    memset(buf, 0, sizeof(buf));
    strcpy(buf, "fig"); (void)vec_push(&sv, buf);
    vec_sort(&sv, vec_cmp_str);
    TF_CHECK_STR((const char *)vec_at(&sv, 0U), "apple");
    TF_CHECK_STR((const char *)vec_at(&sv, 1U), "fig");
    TF_CHECK_STR((const char *)vec_at(&sv, 2U), "pear");
    vec_deinit(&sv);

    tf_end();
}
