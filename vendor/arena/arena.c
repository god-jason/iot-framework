/*
 * Arena（区域/线性）分配器实现。
 *
 * 通过在预分配缓冲区上推进 offset 完成分配，不维护空闲链表；
 * 分配失败不影响已有数据，reset/restore 只移动 offset。
 */
#include "arena.h"

#define ARENA_DEFAULT_ALIGN ((size_t)sizeof(void *))

static size_t arena_align_up(size_t value, size_t align)
{
    return (value + align - 1U) & ~(align - 1U);
}

static int arena_is_pow2(size_t value)
{
    return value != 0U && (value & (value - 1U)) == 0U;
}

void arena_init(arena_t *arena, void *buffer, size_t size)
{
    if (arena == NULL) {
        return;
    }
    arena->base = (uint8_t *)buffer;
    arena->capacity = (buffer != NULL) ? size : 0U;
    arena->offset = 0U;
    arena->peak = 0U;
}

void arena_reset(arena_t *arena)
{
    if (arena == NULL) {
        return;
    }
    arena->offset = 0U;
    arena->peak = 0U;
}

void *arena_alloc_aligned(arena_t *arena, size_t size, size_t align)
{
    uintptr_t abs_addr;
    uintptr_t abs_start;
    size_t start;
    size_t end;

    if (arena == NULL || arena->base == NULL) {
        return NULL;
    }
    if (align <= 1U) {
        align = ARENA_DEFAULT_ALIGN;
    }
    if (!arena_is_pow2(align)) {
        return NULL;
    }

    /* 按绝对地址对齐，允许缓冲区本身未按 align 对齐 */
    abs_addr = (uintptr_t)arena->base + arena->offset;
    if (abs_addr > UINTPTR_MAX - (uintptr_t)(align - 1U)) {
        return NULL;
    }
    abs_start = arena_align_up(abs_addr, align);
    start = (size_t)(abs_start - (uintptr_t)arena->base);
    if (start < arena->offset || start > arena->capacity) {
        return NULL;    /* 对齐后越过缓冲区末尾 */
    }
    if (size > arena->capacity - start) {
        return NULL;    /* 剩余空间不足 */
    }

    end = start + size;
    arena->offset = end;
    if (end > arena->peak) {
        arena->peak = end;
    }
    return arena->base + start;
}

void *arena_alloc(arena_t *arena, size_t size)
{
    return arena_alloc_aligned(arena, size, ARENA_DEFAULT_ALIGN);
}

arena_mark_t arena_mark(const arena_t *arena)
{
    return (arena != NULL) ? arena->offset : 0U;
}

int arena_restore(arena_t *arena, arena_mark_t mark)
{
    if (arena == NULL || mark > arena->offset) {
        return -1;
    }
    arena->offset = mark;
    return 0;
}

size_t arena_used(const arena_t *arena)
{
    return (arena != NULL) ? arena->offset : 0U;
}

size_t arena_remaining(const arena_t *arena)
{
    return (arena != NULL && arena->offset <= arena->capacity)
               ? arena->capacity - arena->offset
               : 0U;
}

size_t arena_peak(const arena_t *arena)
{
    return (arena != NULL) ? arena->peak : 0U;
}
