#ifndef ARENA_H
#define ARENA_H

/*
 * Arena（区域/线性）分配器。
 *
 * 特点：
 *   - 只在预分配缓冲区上做指针推进，分配为 O(1)，不产生碎片；
 *   - 不支持单独 free，只能整体 reset 或回滚到某个 mark；
 *   - 适合生命周期一致的对象（解析临时数据、单帧请求、启动期对象等）。
 *
 * 对齐：默认按 sizeof(void *) 对齐；arena_alloc_aligned 支持任意 2 的幂，
 * 按绝对地址对齐（缓冲区本身无需预先对齐）。
 * 线程安全：无内部锁，多任务环境需调用者自行保护。
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t *base;      /* 缓冲区起始地址 */
    size_t capacity;    /* 缓冲区总字节数 */
    size_t offset;      /* 当前已分配到的偏移 */
    size_t peak;        /* 历史最高水位（用于统计） */
} arena_t;

/* 分配点标记，用于回滚 */
typedef size_t arena_mark_t;

/* 用一段外部缓冲区初始化（不会释放该缓冲区） */
void arena_init(arena_t *arena, void *buffer, size_t size);

/* 复位到初始状态，offset 与 peak 清零 */
void arena_reset(arena_t *arena);

/* 分配 size 字节，默认按 sizeof(void *) 对齐；失败返回 NULL */
void *arena_alloc(arena_t *arena, size_t size);

/* 按 align（2 的幂）对齐分配；align 为 0/1 时按默认对齐 */
void *arena_alloc_aligned(arena_t *arena, size_t size, size_t align);

/* 获取当前分配点（可用于后续回滚） */
arena_mark_t arena_mark(const arena_t *arena);

/*
 * 回滚到 mark。mark 必须不大于当前 offset，否则返回 -1（不改变状态）；
 * 成功返回 0。回滚后 peak 保留历史最高值。
 */
int arena_restore(arena_t *arena, arena_mark_t mark);

/* 已使用字节数 / 剩余字节数 / 历史峰值 */
size_t arena_used(const arena_t *arena);
size_t arena_remaining(const arena_t *arena);
size_t arena_peak(const arena_t *arena);

#ifdef __cplusplus
}
#endif

#endif /* ARENA_H */
