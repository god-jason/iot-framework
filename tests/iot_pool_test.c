/* src/iot_pool.c per-file 单元测试。 */
#include "test_framework.h"
#include "iot_pool.h"

#include <string.h>

void ut_iot_pool(void)
{
    iot_pool_t pool;
    uint64_t storage[8];
    void *p1;
    void *p2;
    void *p3;
    void *p4;

    tf_begin("pool 初始化参数校验与对齐");
    memset(storage, 0, sizeof(storage));
    TF_CHECK_INT(iot_pool_init(NULL, storage, 16, 3), -1);
    TF_CHECK_INT(iot_pool_init(&pool, NULL, 16, 3), -1);
    TF_CHECK_INT(iot_pool_init(&pool, storage, 0, 3), -1);
    TF_CHECK_INT(iot_pool_init(&pool, storage, 16, 0), -1);

    /* 块大小小于指针宽度时被提升 */
    TF_CHECK_INT(iot_pool_init(&pool, storage, 1, 3), IOT_OK);
    TF_CHECK_INT(pool.block_size, sizeof(void *));
    TF_CHECK_INT(pool.block_count, 3);
    TF_CHECK_INT(iot_pool_available(&pool), 3);
    TF_CHECK_INT(iot_pool_used(&pool), 0);

    /* 非对齐块大小向上取整到指针宽度倍数 */
    TF_CHECK_INT(iot_pool_init(&pool, storage, 13, 3), IOT_OK);
    TF_CHECK_INT(pool.block_size, 16);
    tf_end();

    tf_begin("pool 分配与耗尽");
    TF_CHECK_INT(iot_pool_init(&pool, storage, 16, 3), IOT_OK);
    p1 = iot_pool_alloc(&pool);
    p2 = iot_pool_alloc(&pool);
    p3 = iot_pool_alloc(&pool);
    p4 = iot_pool_alloc(&pool);
    TF_CHECK(p1 != NULL);
    TF_CHECK(p2 != NULL);
    TF_CHECK(p3 != NULL);
    TF_CHECK(p1 != p2 && p2 != p3 && p1 != p3);
    TF_CHECK(p4 == NULL);
    TF_CHECK_INT(iot_pool_available(&pool), 0);
    TF_CHECK_INT(iot_pool_used(&pool), 3);

    /* 返回块应位于外部缓冲区范围内且互不重叠 */
    TF_CHECK((uint8_t *)p1 >= (uint8_t *)storage);
    TF_CHECK((uint8_t *)p3 < (uint8_t *)storage + sizeof(storage));
    tf_end();

    tf_begin("pool 释放与复用");
    TF_CHECK_INT(iot_pool_free(&pool, p2), IOT_OK);
    TF_CHECK_INT(iot_pool_available(&pool), 1);
    TF_CHECK_INT(iot_pool_used(&pool), 2);
    p4 = iot_pool_alloc(&pool);
    TF_CHECK(p4 == p2);             /* LIFO 复用 */
    TF_CHECK_INT(iot_pool_available(&pool), 0);

    /* 已分配块可正常读写 */
    memset(p4, 0x5A, 16);
    TF_CHECK_INT(((uint8_t *)p4)[0], 0x5A);
    TF_CHECK_INT(((uint8_t *)p4)[15], 0x5A);

    TF_CHECK_INT(iot_pool_free(&pool, p4), IOT_OK);
    TF_CHECK_INT(iot_pool_free(&pool, p1), IOT_OK);
    TF_CHECK_INT(iot_pool_free(&pool, p3), IOT_OK);
    TF_CHECK_INT(iot_pool_available(&pool), 3);
    TF_CHECK_INT(iot_pool_used(&pool), 0);
    tf_end();

    tf_begin("pool 非法参数与空池");
    TF_CHECK_INT(iot_pool_free(&pool, NULL), -1);
    TF_CHECK_INT(iot_pool_free(NULL, p1), -1);
    TF_CHECK(iot_pool_alloc(NULL) == NULL);
    TF_CHECK_INT(iot_pool_available(NULL), 0);
    TF_CHECK_INT(iot_pool_used(NULL), 0);
    tf_end();

    tf_begin("pool 重新初始化复位");
    TF_CHECK_INT(iot_pool_init(&pool, storage, 16, 3), IOT_OK);
    TF_CHECK_INT(iot_pool_available(&pool), 3);
    TF_CHECK_INT(iot_pool_used(&pool), 0);
    p1 = iot_pool_alloc(&pool);
    TF_CHECK(p1 == (void *)storage);   /* 重新串联后首块回到起始地址 */
    TF_CHECK_INT(iot_pool_available(&pool), 2);

    /* 反复 init 后仍能分配全部块 */
    TF_CHECK_INT(iot_pool_init(&pool, storage, 16, 3), IOT_OK);
    p1 = iot_pool_alloc(&pool);
    p2 = iot_pool_alloc(&pool);
    p3 = iot_pool_alloc(&pool);
    TF_CHECK(p1 != NULL && p2 != NULL && p3 != NULL);
    TF_CHECK(iot_pool_alloc(&pool) == NULL);
    tf_end();
}
