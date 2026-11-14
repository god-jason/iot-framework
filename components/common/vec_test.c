/*
 * vec_test.c - 变长数组回归测试
 */

#include "test.h"
#include "vec.h"

void ut_vec(void)
{
    vec_t v;
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
}
