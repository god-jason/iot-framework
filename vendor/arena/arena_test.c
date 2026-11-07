/* arena.c 单元测试：分配、对齐、溢出、mark/restore 与统计。 */
#include "test.h"
#include "arena.h"

#include <string.h>

static uint8_t s_buf[256];

void ut_arena(void)
{
    arena_t arena;
    uint8_t *p;
    uint8_t *q;
    arena_mark_t mark;

    tf_begin("arena/初始化与基本分配");
    arena_init(&arena, s_buf, sizeof(s_buf));
    TF_CHECK_INT((int)arena_used(&arena), 0);
    TF_CHECK_INT((int)arena_remaining(&arena), (int)sizeof(s_buf));

    p = (uint8_t *)arena_alloc(&arena, 10U);
    TF_CHECK(p != NULL);
    if (p != NULL) {
        TF_CHECK(((uintptr_t)p % sizeof(void *)) == 0U);
        memset(p, 0x11, 10U);
        TF_CHECK_INT((int)arena_used(&arena), 10);
        TF_CHECK_INT((int)arena_peak(&arena), 10);
    }

    q = (uint8_t *)arena_alloc(&arena, 0U);
    TF_CHECK(q != NULL);

    TF_CHECK(arena_alloc(NULL, 1U) == NULL);
    tf_end();

    tf_begin("arena/对齐分配");
    arena_reset(&arena);
    p = (uint8_t *)arena_alloc_aligned(&arena, 20U, 64U);
    TF_CHECK(p != NULL);
    TF_CHECK(((uintptr_t)p & 63U) == 0U);
    TF_CHECK(arena_alloc_aligned(&arena, 8U, 3U) == NULL);

    q = (uint8_t *)arena_alloc_aligned(&arena, 8U, 0U);
    TF_CHECK(q != NULL);
    TF_CHECK(((uintptr_t)q % sizeof(void *)) == 0U);
    tf_end();

    tf_begin("arena/空间不足不改变状态");
    arena_reset(&arena);
    p = (uint8_t *)arena_alloc(&arena, sizeof(s_buf));
    TF_CHECK(p != NULL);
    {
        size_t used = arena_used(&arena);

        TF_CHECK(arena_alloc(&arena, 1U) == NULL);
        TF_CHECK_INT((int)arena_used(&arena), (int)used);
        TF_CHECK_INT((int)arena_remaining(&arena), 0);
    }
    tf_end();

    tf_begin("arena/mark 与 restore");
    arena_reset(&arena);
    (void)arena_alloc(&arena, 32U);
    mark = arena_mark(&arena);
    (void)arena_alloc(&arena, 64U);
    TF_CHECK_INT((int)arena_used(&arena), 96);
    TF_CHECK_INT(arena_restore(&arena, mark), 0);
    TF_CHECK_INT((int)arena_used(&arena), 32);
    TF_CHECK_INT(arena_restore(&arena, mark + 999U), -1);
    TF_CHECK_INT(arena_restore(NULL, 0U), -1);
    tf_end();

    tf_begin("arena/reset 与 peak");
    arena_reset(&arena);
    (void)arena_alloc(&arena, 128U);
    TF_CHECK_INT((int)arena_peak(&arena), 128);
    arena_reset(&arena);
    TF_CHECK_INT((int)arena_used(&arena), 0);
    TF_CHECK_INT((int)arena_peak(&arena), 0);
    TF_CHECK_INT((int)arena_remaining(&arena), (int)sizeof(s_buf));

    arena_init(&arena, NULL, 100U);
    TF_CHECK_INT((int)arena_remaining(&arena), 0);
    TF_CHECK(arena_alloc(&arena, 1U) == NULL);
    tf_end();
}
