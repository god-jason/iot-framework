#ifndef TLSF_H
#define TLSF_H

/*
 * TLSF（Two-Level Segregated Fit）动态内存分配器。
 *
 * 特点：
 *   - malloc/free 为 O(1)：两级位图 + 空闲链表快速定位合适的块；
 *   - 无动态元数据，控制结构和管理区都由调用者提供；
 *   - 块头不覆盖用户数据，实现简单、可读性好；
 *   - 支持相邻空闲块合并，降低碎片。
 *
 * 使用方式：
 *   static uint8_t heap[64 * 1024] __attribute__((aligned(8)));
 *   tlsf_t t = tlsf_create_with_pool(heap, sizeof(heap));
 *   void *p = tlsf_malloc(t, 128);
 *   ...
 *   tlsf_free(t, p);
 *
 * 说明：本实现不是线程安全的，多任务环境需由调用者加锁。
 */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void *tlsf_t;   /* 控制结构句柄 */
typedef void *pool_t;   /* 内存池句柄（池内首个块） */

/* 控制结构/对齐/块大小等尺寸查询，便于调用者规划内存 */
size_t tlsf_size(void);
size_t tlsf_align_size(void);
size_t tlsf_block_size_min(void);
size_t tlsf_block_size_max(void);
size_t tlsf_pool_overhead(void);
size_t tlsf_alloc_overhead(void);

/*
 * 在 mem/size 指定的内存中创建分配器控制结构（不添加内存池）。
 * 返回 NULL 表示内存不足或对齐失败。
 */
tlsf_t tlsf_create(void *mem, size_t size);

/*
 * 创建控制结构并把剩余空间作为初始内存池。
 * 这是最常用的入口。
 */
tlsf_t tlsf_create_with_pool(void *mem, size_t size);

/* 获取初始内存池句柄（tlsf_create_with_pool 创建的那个池） */
pool_t tlsf_get_pool(tlsf_t tlsf);

/*
 * 追加一个内存池。mem 会自动向上对齐，bytes 向下对齐。
 * 返回池句柄；失败返回 NULL。
 */
pool_t tlsf_add_pool(tlsf_t tlsf, void *mem, size_t bytes);

/*
 * 移除一个完全空闲的内存池（池内块必须仍是一个整体空闲块）。
 * 若池已分配或被拆分，本函数不做任何操作。
 */
void tlsf_remove_pool(tlsf_t tlsf, pool_t pool);

/* 分配/释放；失败返回 NULL。tlsf_malloc(t, 0) 返回最小块 */
void *tlsf_malloc(tlsf_t tlsf, size_t bytes);
void tlsf_free(tlsf_t tlsf, void *ptr);

/* 分配按 align（2 的幂）对齐的内存；align <= tlsf_align_size() 等价 malloc */
void *tlsf_memalign(tlsf_t tlsf, size_t align, size_t bytes);

/*
 * 调整已分配块大小：
 *   - 缩小：原地切分，多余部分归还空闲表；
 *   - 扩大：优先合并相邻空闲块，否则重新分配并拷贝。
 * ptr 为 NULL 时等价 malloc；size 为 0 时释放并返回 NULL。
 */
void *tlsf_realloc(tlsf_t tlsf, void *ptr, size_t size);

/* 返回 ptr 所在块可用的用户字节数 */
size_t tlsf_block_size(void *ptr);

#ifdef __cplusplus
}
#endif

#endif /* TLSF_H */
