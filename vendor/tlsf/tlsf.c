/*
 * TLSF（Two-Level Segregated Fit）分配器实现。
 *
 * 结构：
 *   - 控制结构 tlsf_control_t 放在调用者内存起始处，保存两级位图与
 *     FL x SL 的空闲链表头；
 *   - 每个内存池由若干物理相邻的块组成，末尾放一个 size=0 的哨兵块，
 *     便于合并时安全访问“下一个物理块”；
 *   - 块头对用户数据不重叠，用户指针 = 块头 + sizeof(tlsf_block_t)。
 *
 * 复杂度：malloc/free 通过位图定位，均为 O(1)（忽略链表操作常数）。
 */
#include "tlsf.h"

#include <stdint.h>
#include <string.h>

#if defined(_WIN64) || defined(__x86_64__) || defined(__aarch64__) || \
    defined(__LP64__) || defined(__LLP64__)
#define TLSF_64BIT 1
#endif

#define TLSF_ALIGN          ((size_t)sizeof(void *))
#define TLSF_ALIGN_MASK     (TLSF_ALIGN - 1U)
#define TLSF_SL_LOG2        5
#define TLSF_SL_COUNT       (1U << TLSF_SL_LOG2)
#define TLSF_FL_SHIFT       (TLSF_SL_LOG2 + 2)              /* 7 */
#ifdef TLSF_64BIT
#define TLSF_FL_MAX         38
#define TLSF_MAX_SIZE       (((size_t)1) << 38)
#else
#define TLSF_FL_MAX         32
#define TLSF_MAX_SIZE       ((size_t)0x7FFFFFFFU)
#endif
#define TLSF_FL_COUNT       (TLSF_FL_MAX - TLSF_FL_SHIFT + 1)
#define TLSF_SMALL_BLOCK    ((size_t)1 << TLSF_FL_SHIFT)    /* 128 */
#define TLSF_BLOCK_FREE     ((size_t)1)
#define TLSF_SIZE_MASK      (~(size_t)TLSF_ALIGN_MASK)

/* 块头；size 的最低位为空闲标志，其余位为对齐后的块总大小 */
typedef struct tlsf_block {
    struct tlsf_block *prev_phys;   /* 上一个物理相邻块（池首块为 NULL） */
    size_t size;                    /* 块大小 | 空闲标志 */
    struct tlsf_block *next_free;   /* 空闲链表后继 */
    struct tlsf_block *prev_free;   /* 空闲链表前驱 */
} tlsf_block_t;

typedef struct {
    uint32_t fl_bitmap;                             /* 一级位图 */
    uint32_t sl_bitmap[TLSF_FL_COUNT];              /* 二级位图 */
    tlsf_block_t *blocks[TLSF_FL_COUNT][TLSF_SL_COUNT];
    tlsf_block_t *pool;                             /* 首个内存池（供查询） */
} tlsf_control_t;

#define TLSF_MIN_BLOCK \
    ((sizeof(tlsf_block_t) + TLSF_ALIGN_MASK) & TLSF_SIZE_MASK)

#define block_size(b)       ((b)->size & TLSF_SIZE_MASK)
#define block_is_free(b)    (((b)->size & TLSF_BLOCK_FREE) != 0U)

static size_t align_down(size_t v)
{
    return v & TLSF_SIZE_MASK;
}

static size_t align_up(size_t v)
{
    return (v + TLSF_ALIGN_MASK) & TLSF_SIZE_MASK;
}

static uintptr_t align_up_ptr(uintptr_t v)
{
    return (v + (uintptr_t)TLSF_ALIGN_MASK) & ~(uintptr_t)TLSF_ALIGN_MASK;
}

/* 返回最高置位的下标（0 起）；word 为 0 时返回 -1 */
static int tlsf_fls32(uint32_t word)
{
    int bit = -1;

    while (word != 0U) {
        word >>= 1;
        bit++;
    }
    return bit;
}

static int tlsf_fls_size(size_t word)
{
    int bit = -1;

    while (word != 0U) {
        word >>= 1;
        bit++;
    }
    return bit;
}

/* 返回最低置位的下标（0 起）；word 为 0 时返回 -1 */
static int tlsf_ffs32(uint32_t word)
{
    int bit = 0;

    if (word == 0U) {
        return -1;
    }
    while ((word & 1U) == 0U) {
        word >>= 1;
        bit++;
    }
    return bit;
}

/* 由块大小映射到一级/二级索引 */
static void mapping_insert(size_t size, int *fl, int *sl)
{
    if (size < TLSF_SMALL_BLOCK) {
        *fl = 0;
        *sl = (int)(size / (TLSF_SMALL_BLOCK / TLSF_SL_COUNT));
    } else {
        int fl_raw = tlsf_fls_size(size);

        *sl = (int)((size >> (fl_raw - TLSF_SL_LOG2)) ^ TLSF_SL_COUNT);
        *fl = fl_raw - (TLSF_FL_SHIFT - 1);
    }
}

/* 向上取整到二级索引边界，保证搜索到的块不小于请求 */
static void mapping_search(size_t size, int *fl, int *sl)
{
    if (size >= TLSF_SMALL_BLOCK) {
        int fl_raw = tlsf_fls_size(size);

        size += ((size_t)1 << (fl_raw - TLSF_SL_LOG2)) - 1U;
    }
    mapping_insert(size, fl, sl);
}

static tlsf_block_t *block_next(const tlsf_block_t *b)
{
    return (tlsf_block_t *)((uint8_t *)b + block_size(b));
}

static void insert_free(tlsf_control_t *c, tlsf_block_t *b)
{
    int fl;
    int sl;

    mapping_insert(block_size(b), &fl, &sl);
    b->size = block_size(b) | TLSF_BLOCK_FREE;
    b->prev_free = NULL;
    b->next_free = c->blocks[fl][sl];
    if (b->next_free != NULL) {
        b->next_free->prev_free = b;
    }
    c->blocks[fl][sl] = b;
    c->sl_bitmap[fl] |= (1U << sl);
    c->fl_bitmap |= (1U << fl);
}

static void remove_free(tlsf_control_t *c, tlsf_block_t *b)
{
    int fl;
    int sl;

    mapping_insert(block_size(b), &fl, &sl);
    if (b->prev_free != NULL) {
        b->prev_free->next_free = b->next_free;
    } else {
        c->blocks[fl][sl] = b->next_free;
    }
    if (b->next_free != NULL) {
        b->next_free->prev_free = b->prev_free;
    }
    b->next_free = NULL;
    b->prev_free = NULL;
    b->size &= ~TLSF_BLOCK_FREE;

    if (c->blocks[fl][sl] == NULL) {
        c->sl_bitmap[fl] &= ~(1U << sl);
        if (c->sl_bitmap[fl] == 0U) {
            c->fl_bitmap &= ~(1U << fl);
        }
    }
}

/* 按大小找到首个合适空闲块，返回 NULL 表示无可用块 */
static tlsf_block_t *find_suitable(tlsf_control_t *c, size_t size)
{
    uint32_t sl_map;
    uint32_t fl_map;
    int fl;
    int sl;

    mapping_search(size, &fl, &sl);
    if (fl < 0 || fl >= TLSF_FL_COUNT) {
        return NULL;
    }

    sl_map = c->sl_bitmap[fl] & (~0U << sl);
    if (sl_map == 0U) {
        if (fl + 1 >= TLSF_FL_COUNT) {
            return NULL;
        }
        fl_map = c->fl_bitmap & (~0U << (fl + 1));
        if (fl_map == 0U) {
            return NULL;
        }
        fl = tlsf_ffs32(fl_map);
        sl_map = c->sl_bitmap[fl];
        if (sl_map == 0U) {
            return NULL;
        }
    }
    sl = tlsf_ffs32(sl_map);
    return c->blocks[fl][sl];
}

/* 从空闲链表取块并按需切分 */
static void *do_malloc(tlsf_control_t *c, size_t bytes)
{
    tlsf_block_t *block;
    size_t size;

    if (c == NULL) {
        return NULL;
    }

    size = bytes + sizeof(tlsf_block_t);
    if (size < bytes) {
        return NULL;                    /* 溢出 */
    }
    if (size > TLSF_MAX_SIZE - TLSF_ALIGN) {
        return NULL;
    }
    if (size < TLSF_MIN_BLOCK) {
        size = TLSF_MIN_BLOCK;          /* 至少容纳块头 */
    }
    size = align_up(size);

    block = find_suitable(c, size);
    if (block == NULL) {
        return NULL;
    }
    remove_free(c, block);

    if (block_size(block) >= size + TLSF_MIN_BLOCK) {
        tlsf_block_t *rest = (tlsf_block_t *)((uint8_t *)block + size);

        rest->prev_phys = block;
        rest->size = block_size(block) - size;
        rest->next_free = NULL;
        rest->prev_free = NULL;
        block->size = size;                       /* 标记为已分配 */
        block_next(rest)->prev_phys = rest;
        insert_free(c, rest);
    } else {
        block->size = block_size(block);          /* 清除空闲标志 */
    }

    return (uint8_t *)block + sizeof(tlsf_block_t);
}

/* 释放块并合并相邻空闲块 */
static void do_free(tlsf_control_t *c, tlsf_block_t *block)
{
    tlsf_block_t *next;
    tlsf_block_t *prev;

    block->size = block_size(block);
    next = block_next(block);
    if (block_is_free(next)) {
        remove_free(c, next);
        block->size += block_size(next);
    }

    prev = block->prev_phys;
    if (prev != NULL && block_is_free(prev)) {
        remove_free(c, prev);
        prev->size = block_size(prev) + block_size(block);
        block = prev;
    }

    block_next(block)->prev_phys = block;
    insert_free(c, block);
}

/* 将已分配块缩小到 bytes（用于 memalign 后回收尾部） */
static void *shrink_alloc(tlsf_control_t *c, tlsf_block_t *block, size_t bytes)
{
    size_t size = bytes + sizeof(tlsf_block_t);
    size_t old = block_size(block);

    if (size < bytes) {
        return NULL;                    /* 溢出 */
    }
    if (size < TLSF_MIN_BLOCK) {
        size = TLSF_MIN_BLOCK;
    }
    size = align_up(size);
    if (size > old) {
        return NULL;
    }
    if (old >= size + TLSF_MIN_BLOCK) {
        tlsf_block_t *rest = (tlsf_block_t *)((uint8_t *)block + size);

        rest->prev_phys = block;
        rest->size = old - size;
        block->size = size;
        block_next(rest)->prev_phys = rest;
        do_free(c, rest);
    }
    return (uint8_t *)block + sizeof(tlsf_block_t);
}

size_t tlsf_size(void)
{
    return align_up(sizeof(tlsf_control_t));
}

size_t tlsf_align_size(void)
{
    return TLSF_ALIGN;
}

size_t tlsf_block_size_min(void)
{
    return TLSF_MIN_BLOCK;
}

size_t tlsf_block_size_max(void)
{
    return TLSF_MAX_SIZE;
}

size_t tlsf_pool_overhead(void)
{
    return sizeof(tlsf_block_t);   /* 池尾哨兵块 */
}

size_t tlsf_alloc_overhead(void)
{
    return sizeof(tlsf_block_t);   /* 每个已分配块的块头 */
}

tlsf_t tlsf_create(void *mem, size_t size)
{
    uintptr_t start;
    size_t offset;
    tlsf_control_t *c;

    if (mem == NULL) {
        return NULL;
    }
    start = align_up_ptr((uintptr_t)mem);
    offset = (size_t)(start - (uintptr_t)mem);
    if (size <= offset || size - offset < tlsf_size()) {
        return NULL;
    }

    c = (tlsf_control_t *)start;
    memset(c, 0, sizeof(*c));
    return (tlsf_t)c;
}

pool_t tlsf_add_pool(tlsf_t tlsf, void *mem, size_t bytes)
{
    tlsf_control_t *c = (tlsf_control_t *)tlsf;
    uintptr_t start;
    size_t offset;
    size_t size;
    size_t main_size;
    tlsf_block_t *block;
    tlsf_block_t *sentinel;

    if (c == NULL || mem == NULL) {
        return NULL;
    }

    start = align_up_ptr((uintptr_t)mem);
    offset = (size_t)(start - (uintptr_t)mem);
    if (bytes <= offset) {
        return NULL;
    }
    size = align_down(bytes - offset);
    if (size < TLSF_MIN_BLOCK + sizeof(tlsf_block_t)) {
        return NULL;
    }

    main_size = align_down(size - sizeof(tlsf_block_t));
    if (main_size < TLSF_MIN_BLOCK) {
        return NULL;
    }

    block = (tlsf_block_t *)start;
    block->prev_phys = NULL;
    block->size = main_size;
    block->next_free = NULL;
    block->prev_free = NULL;

    sentinel = (tlsf_block_t *)((uint8_t *)block + main_size);
    sentinel->prev_phys = block;
    sentinel->size = 0U;                 /* 哨兵：非空闲，阻止非法合并 */
    sentinel->next_free = NULL;
    sentinel->prev_free = NULL;

    insert_free(c, block);
    if (c->pool == NULL) {
        c->pool = block;
    }
    return (pool_t)block;
}

tlsf_t tlsf_create_with_pool(void *mem, size_t size)
{
    tlsf_t t;
    uintptr_t start;
    size_t offset;

    t = tlsf_create(mem, size);
    if (t == NULL) {
        return NULL;
    }
    start = align_up_ptr((uintptr_t)mem + tlsf_size());
    offset = (size_t)(start - (uintptr_t)mem);
    if (size <= offset) {
        return NULL;
    }
    if (tlsf_add_pool(t, (void *)start, size - offset) == NULL) {
        return NULL;
    }
    return t;
}

pool_t tlsf_get_pool(tlsf_t tlsf)
{
    tlsf_control_t *c = (tlsf_control_t *)tlsf;

    return (c != NULL) ? (pool_t)c->pool : NULL;
}

void tlsf_remove_pool(tlsf_t tlsf, pool_t pool)
{
    tlsf_control_t *c = (tlsf_control_t *)tlsf;
    tlsf_block_t *block = (tlsf_block_t *)pool;

    if (c == NULL || block == NULL || !block_is_free(block)) {
        return;
    }
    remove_free(c, block);
    if (c->pool == block) {
        c->pool = NULL;
    }
}

void *tlsf_malloc(tlsf_t tlsf, size_t bytes)
{
    return do_malloc((tlsf_control_t *)tlsf, bytes);
}

void tlsf_free(tlsf_t tlsf, void *ptr)
{
    tlsf_control_t *c = (tlsf_control_t *)tlsf;

    if (c == NULL || ptr == NULL) {
        return;
    }
    do_free(c, (tlsf_block_t *)((uint8_t *)ptr - sizeof(tlsf_block_t)));
}

void *tlsf_memalign(tlsf_t tlsf, size_t align, size_t bytes)
{
    tlsf_control_t *c = (tlsf_control_t *)tlsf;
    tlsf_block_t *block;
    tlsf_block_t *new_block;
    tlsf_block_t *after;
    uint8_t *raw;
    uintptr_t aligned;
    size_t adjust;
    size_t total;

    if (c == NULL) {
        return NULL;
    }
    if (align <= TLSF_ALIGN) {
        return tlsf_malloc(tlsf, bytes);
    }
    if ((align & (align - 1U)) != 0U) {
        return NULL;
    }

    total = bytes + align + sizeof(tlsf_block_t) + TLSF_MIN_BLOCK;
    raw = (uint8_t *)tlsf_malloc(tlsf, total);
    if (raw == NULL) {
        return NULL;
    }

    aligned = ((uintptr_t)raw + (uintptr_t)align - 1U) &
              ~(uintptr_t)(align - 1U);
    adjust = (size_t)(aligned - (uintptr_t)raw);

    /* 前缀至少要容纳一个最小块，否则继续向后对齐 */
    while (adjust < sizeof(tlsf_block_t) + TLSF_MIN_BLOCK) {
        aligned += (uintptr_t)align;
        adjust += align;
        if (adjust >= total) {
            tlsf_free(tlsf, raw);
            return NULL;
        }
    }

    block = (tlsf_block_t *)(raw - sizeof(tlsf_block_t));
    new_block = (tlsf_block_t *)((uint8_t *)aligned - sizeof(tlsf_block_t));
    after = block_next(block);

    new_block->prev_phys = block;
    new_block->size = block_size(block) -
                      (size_t)((uint8_t *)new_block - (uint8_t *)block);
    block->size = (size_t)((uint8_t *)new_block - (uint8_t *)block);
    after->prev_phys = new_block;

    /* 前缀块归还空闲表（会与前一空闲块合并） */
    do_free(c, block);

    return shrink_alloc(c, new_block, bytes);
}

void *tlsf_realloc(tlsf_t tlsf, void *ptr, size_t size)
{
    tlsf_control_t *c = (tlsf_control_t *)tlsf;
    tlsf_block_t *block;
    size_t old;
    size_t want;

    if (c == NULL) {
        return NULL;
    }
    if (ptr == NULL) {
        return tlsf_malloc(tlsf, size);
    }
    if (size == 0U) {
        tlsf_free(tlsf, ptr);
        return NULL;
    }

    block = (tlsf_block_t *)((uint8_t *)ptr - sizeof(tlsf_block_t));
    old = block_size(block);
    want = size + sizeof(tlsf_block_t);
    if (want < size) {
        return NULL;                    /* 溢出 */
    }
    if (want < TLSF_MIN_BLOCK) {
        want = TLSF_MIN_BLOCK;
    }
    if (want > TLSF_MAX_SIZE - TLSF_ALIGN) {
        return NULL;
    }
    want = align_up(want);

    if (want <= old) {
        (void)shrink_alloc(c, block, size);
        return ptr;
    }

    {
        tlsf_block_t *next = block_next(block);

        if (block_is_free(next) && old + block_size(next) >= want) {
            size_t combined = old + block_size(next);

            remove_free(c, next);
            if (combined >= want + TLSF_MIN_BLOCK) {
                tlsf_block_t *rest =
                    (tlsf_block_t *)((uint8_t *)block + want);

                rest->prev_phys = block;
                rest->size = combined - want;
                block->size = want;
                block_next(rest)->prev_phys = rest;
                do_free(c, rest);
            } else {
                block->size = combined;
                block_next(block)->prev_phys = block;
            }
            return ptr;
        }
    }

    {
        void *np = tlsf_malloc(tlsf, size);

        if (np == NULL) {
            return NULL;
        }
        memcpy(np, ptr, old - sizeof(tlsf_block_t));
        tlsf_free(tlsf, ptr);
        return np;
    }
}

size_t tlsf_block_size(void *ptr)
{
    tlsf_block_t *block;

    if (ptr == NULL) {
        return 0U;
    }
    block = (tlsf_block_t *)((uint8_t *)ptr - sizeof(tlsf_block_t));
    return block_size(block) - sizeof(tlsf_block_t);
}
