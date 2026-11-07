/* tlsf.c 单元测试：创建、分配/释放、对齐、重分配与碎片合并。 */
#include "test.h"
#include "tlsf.h"

#include <stdint.h>
#include <string.h>

#define TLSF_HEAP_WORDS 8192U   /* 64 KiB */

static uint64_t s_heap[TLSF_HEAP_WORDS];
static uint64_t s_pool2[512];   /* 4 KiB，用于追加内存池 */

static void test_create(void)
{
    tlsf_t t;

    tf_begin("tlsf/创建与尺寸查询");
    TF_CHECK(tlsf_size() > 0U);
    TF_CHECK(tlsf_align_size() >= sizeof(void *));
    TF_CHECK(tlsf_block_size_min() > 0U);
    TF_CHECK(tlsf_block_size_max() > tlsf_block_size_min());
    TF_CHECK(tlsf_pool_overhead() > 0U);

    TF_CHECK(tlsf_create(NULL, sizeof(s_heap)) == NULL);
    TF_CHECK(tlsf_create(s_heap, 16U) == NULL);

    t = tlsf_create_with_pool(s_heap, sizeof(s_heap));
    TF_CHECK(t != NULL);
    TF_CHECK(tlsf_get_pool(t) != NULL);
    tf_end();
}

static void test_alloc_free(void)
{
    static const size_t sizes[] = { 1U, 8U, 16U, 64U, 200U, 1000U, 4000U };
    uint8_t *ptrs[sizeof(sizes) / sizeof(sizes[0])];
    tlsf_t t = tlsf_create_with_pool(s_heap, sizeof(s_heap));
    size_t i;

    tf_begin("tlsf/不同大小分配与内容保持");
    TF_CHECK(t != NULL);
    for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        ptrs[i] = (uint8_t *)tlsf_malloc(t, sizes[i]);
        TF_CHECK(ptrs[i] != NULL);
        if (ptrs[i] != NULL) {
            TF_CHECK(((uintptr_t)ptrs[i] % tlsf_align_size()) == 0U);
            TF_CHECK(tlsf_block_size(ptrs[i]) >= sizes[i]);
            memset(ptrs[i], (int)(0xA0U + i), sizes[i]);
        }
    }
    for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        if (ptrs[i] != NULL) {
            size_t j;
            int ok = 1;

            for (j = 0; j < sizes[i]; j++) {
                if (ptrs[i][j] != (uint8_t)(0xA0U + i)) {
                    ok = 0;
                    break;
                }
            }
            TF_CHECK(ok);
            tlsf_free(t, ptrs[i]);
        }
    }
    tf_end();

    tf_begin("tlsf/零长度与 NULL 释放");
    {
        void *p = tlsf_malloc(t, 0U);

        TF_CHECK(p != NULL);
        TF_CHECK(tlsf_block_size(p) >= tlsf_block_size_min() -
                                       tlsf_alloc_overhead());
        tlsf_free(t, p);
        tlsf_free(t, NULL);
    }
    tf_end();
}

static void test_alignment(void)
{
    static const size_t aligns[] = { 16U, 32U, 64U, 256U };
    tlsf_t t = tlsf_create_with_pool(s_heap, sizeof(s_heap));
    size_t i;

    tf_begin("tlsf/memalign 对齐");
    for (i = 0; i < sizeof(aligns) / sizeof(aligns[0]); i++) {
        void *p = tlsf_memalign(t, aligns[i], 100U);

        TF_CHECK(p != NULL);
        if (p != NULL) {
            TF_CHECK(((uintptr_t)p & (aligns[i] - 1U)) == 0U);
            TF_CHECK(tlsf_block_size(p) >= 100U);
            memset(p, 0x5A, 100U);
            tlsf_free(t, p);
        }
    }
    TF_CHECK(tlsf_memalign(t, 24U, 16U) == NULL);   /* 非 2 的幂 */
    TF_CHECK(tlsf_memalign(t, tlsf_align_size(), 16U) != NULL);
    tf_end();
}

static void test_realloc(void)
{
    tlsf_t t = tlsf_create_with_pool(s_heap, sizeof(s_heap));
    uint8_t *p;
    uint8_t *q;
    uint8_t *grown;
    size_t i;
    int ok = 1;

    tf_begin("tlsf/realloc 扩大与缩小");
    p = (uint8_t *)tlsf_malloc(t, 64U);
    TF_CHECK(p != NULL);
    if (p != NULL) {
        for (i = 0; i < 64U; i++) {
            p[i] = (uint8_t)i;
        }

        q = (uint8_t *)tlsf_realloc(t, p, 200U);
        TF_CHECK(q != NULL);
        if (q != NULL) {
            for (i = 0; i < 64U; i++) {
                if (q[i] != (uint8_t)i) {
                    ok = 0;
                    break;
                }
            }
            TF_CHECK(ok);
            TF_CHECK(tlsf_block_size(q) >= 200U);
            memset(q, 0x3C, 200U);
            q = (uint8_t *)tlsf_realloc(t, q, 32U);
            TF_CHECK(q != NULL);
            if (q != NULL) {
                TF_CHECK(tlsf_block_size(q) >= 32U);
                TF_CHECK(q[0] == 0x3C);
                tlsf_free(t, q);
            }
        }
    }

    TF_CHECK(tlsf_realloc(t, NULL, 48U) != NULL);
    p = (uint8_t *)tlsf_malloc(t, 16U);
    TF_CHECK(p != NULL);
    TF_CHECK(tlsf_realloc(t, p, 0U) == NULL);
    tf_end();

    tf_begin("tlsf/realloc 合并相邻空闲块");
    p = (uint8_t *)tlsf_malloc(t, 64U);
    q = (uint8_t *)tlsf_malloc(t, 128U);
    TF_CHECK(p != NULL && q != NULL);
    tlsf_free(t, q);
    grown = (uint8_t *)tlsf_realloc(t, p, 128U);
    TF_CHECK(grown != NULL);
    TF_CHECK(tlsf_block_size(grown) >= 128U);
    tlsf_free(t, grown);
    tf_end();
}

static void test_coalesce_and_exhaust(void)
{
    tlsf_t t = tlsf_create_with_pool(s_heap, sizeof(s_heap));
    void *a;
    void *b;
    void *c;

    tf_begin("tlsf/相邻空闲块合并");
    a = tlsf_malloc(t, 256U);
    b = tlsf_malloc(t, 256U);
    c = tlsf_malloc(t, 256U);
    TF_CHECK(a != NULL && b != NULL && c != NULL);
    tlsf_free(t, b);
    tlsf_free(t, a);
    {
        void *big = tlsf_malloc(t, 400U);

        TF_CHECK(big != NULL);   /* 依赖 a+b 合并后的空间 */
        tlsf_free(t, big);
    }
    tlsf_free(t, c);
    tf_end();

    tf_begin("tlsf/耗尽与全部释放后复用");
    {
        void *blocks[256];
        int count = 0;

        while (count < 256) {
            void *p = tlsf_malloc(t, 64U);

            if (p == NULL) {
                break;
            }
            blocks[count++] = p;
        }
        TF_CHECK(count > 8);
        while (count > 0) {
            tlsf_free(t, blocks[--count]);
        }
        {
            void *p = tlsf_malloc(t, 4096U);

            TF_CHECK(p != NULL);
            tlsf_free(t, p);
        }
    }
    tf_end();

    tf_begin("tlsf/追加与移除内存池");
    {
        pool_t pool = tlsf_add_pool(t, s_pool2, sizeof(s_pool2));

        TF_CHECK(pool != NULL);
        tlsf_remove_pool(t, pool);
        TF_CHECK(tlsf_add_pool(t, NULL, 1024U) == NULL);
        TF_CHECK(tlsf_add_pool(t, s_pool2, 32U) == NULL);
    }
    tf_end();
}

void ut_tlsf(void)
{
    test_create();
    test_alloc_free();
    test_alignment();
    test_realloc();
    test_coalesce_and_exhaust();
}
