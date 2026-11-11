/*
 * iot_mem.c - 内核统一内存接口
 *
 * 后端选择：
 *   - 默认使用 libc 的 malloc / realloc / free（PC / AVR）；
 *   - 定义 IOT_MEM_PORT 时由平台提供 port_mem_*（STM32 用 TLSF 静态堆），
 *     并通过 port_mem_info() 上报堆容量。
 *
 * 任务统计（IOT_USE_MEM_STATS）：
 *   - 每次分配在用户指针前放一个指针对齐的头，记录请求字节数与所属任务 tid；
 *   - 释放时按 tid 找到任务并扣减，任务已销毁则只释放内存；
 *   - 代价：每次分配多 sizeof(iot_mem_hdr_t) 字节，分配/释放各一次注册表查找。
 */
#include "iot_config.h"
#include "iot.h"
#include "iot_mem.h"

#include <stdlib.h>
#include <string.h>

/* ------------------------------ 平台后端 ------------------------------ */

#if defined(IOT_MEM_PORT)
/* 由平台实现（platforms/stm32/stm32_mem_tlsf.c） */
void *port_mem_malloc(size_t size);
void *port_mem_realloc(void *ptr, size_t size);
void port_mem_free(void *ptr);
int port_mem_info(iot_mem_info_t *out);
#else
#ifndef port_mem_malloc
#define port_mem_malloc malloc
#endif
#ifndef port_mem_realloc
#define port_mem_realloc realloc
#endif
#ifndef port_mem_free
#define port_mem_free free
#endif
#endif /* IOT_MEM_PORT */

/* ------------------------------ 统计头 ------------------------------ */

#if IOT_USE_MEM_STATS

/*
 * 分配头：首地址即 port_mem_* 返回的原始指针，用户指针为 raw + 头长。
 * 用 union 保证与最大对齐一致（x86-64 下 16 字节，ARM32 下 8 字节）。
 */
typedef union {
    struct {
        uint32_t size;      /* 用户请求字节数 */
        uint32_t tid;       /* 所属任务 tid；0 表示非任务上下文 */
    } h;
    void *ptr_align;
    long double ld_align;
} iot_mem_hdr_t;

static void iot_mem_stat_add(uint32_t tid, uint32_t size)
{
    iot_task_t *task;

    if (size == 0U || tid == 0U) {
        return;
    }
    for (task = iot_task_first(); task != NULL; task = iot_task_next(task)) {
        if (task->tid == tid) {
            task->mem_used += size;
            if (task->mem_used > task->mem_peak) {
                task->mem_peak = task->mem_used;
            }
            return;
        }
    }
}

static void iot_mem_stat_sub(uint32_t tid, uint32_t size)
{
    iot_task_t *task;

    if (size == 0U || tid == 0U) {
        return;
    }
    for (task = iot_task_first(); task != NULL; task = iot_task_next(task)) {
        if (task->tid == tid) {
            task->mem_used = (task->mem_used >= size) ? (task->mem_used - size)
                                                      : 0U;
            return;
        }
    }
}

#define IOT_MEM_HDR_SIZE ((size_t)sizeof(iot_mem_hdr_t))

/* 全局已用（用户字节）与“非任务上下文”分配量（如 main 里创建的实例） */
static uint32_t s_used_total;
static uint32_t s_used_untracked;

/* 记账入口：总量始终累加，再由 tid 决定记到任务还是“未归属” */
static void iot_mem_account_add(uint32_t tid, uint32_t size)
{
    if (size == 0U) {
        return;
    }
    s_used_total += size;
    if (tid == 0U) {
        s_used_untracked += size;
        return;
    }
    iot_mem_stat_add(tid, size);
}

static void iot_mem_account_sub(uint32_t tid, uint32_t size)
{
    if (size == 0U) {
        return;
    }
    s_used_total = (s_used_total >= size) ? (s_used_total - size) : 0U;
    if (tid == 0U) {
        s_used_untracked = (s_used_untracked >= size)
            ? (s_used_untracked - size) : 0U;
        return;
    }
    iot_mem_stat_sub(tid, size);
}

#endif /* IOT_USE_MEM_STATS */

/* ------------------------------ 分配接口 ------------------------------ */

void *iot_mem_malloc(size_t size)
{
#if IOT_USE_MEM_STATS
    iot_mem_hdr_t *hdr;
    void *raw;

    if (size > (size_t)-1 - IOT_MEM_HDR_SIZE) {
        return NULL;
    }
    raw = port_mem_malloc(size + IOT_MEM_HDR_SIZE);
    if (raw == NULL) {
        return NULL;
    }
    hdr = (iot_mem_hdr_t *)raw;
    hdr->h.size = (uint32_t)size;
    hdr->h.tid = (iot_current_task != NULL) ? iot_current_task->tid : 0U;
    iot_mem_account_add(hdr->h.tid, hdr->h.size);
    return (void *)((uint8_t *)raw + IOT_MEM_HDR_SIZE);
#else
    return port_mem_malloc(size);
#endif
}

void *iot_mem_calloc(size_t count, size_t size)
{
    void *ptr;
    size_t total;

    if (count != 0U && size > ((size_t)-1) / count) {
        return NULL;
    }
    total = count * size;
    ptr = iot_mem_malloc(total);
    if (ptr != NULL) {
        memset(ptr, 0, total);
    }
    return ptr;
}

void *iot_mem_realloc(void *ptr, size_t size)
{
#if IOT_USE_MEM_STATS
    iot_mem_hdr_t *hdr;
    iot_mem_hdr_t *out;
    uint32_t old_size;
    uint32_t tid;

    if (ptr == NULL) {
        return iot_mem_malloc(size);
    }
    if (size == 0U) {
        iot_mem_free(ptr);
        return NULL;
    }
    if (size > (size_t)-1 - IOT_MEM_HDR_SIZE) {
        return NULL;
    }

    hdr = (iot_mem_hdr_t *)((uint8_t *)ptr - IOT_MEM_HDR_SIZE);
    old_size = hdr->h.size;
    tid = hdr->h.tid;

    out = (iot_mem_hdr_t *)port_mem_realloc(hdr, size + IOT_MEM_HDR_SIZE);
    if (out == NULL) {
        return NULL;
    }
    out->h.size = (uint32_t)size;
    out->h.tid = tid;

    if (size > old_size) {
        iot_mem_account_add(tid, (uint32_t)size - old_size);
    } else if (size < old_size) {
        iot_mem_account_sub(tid, old_size - (uint32_t)size);
    }
    return (void *)((uint8_t *)out + IOT_MEM_HDR_SIZE);
#else
    return port_mem_realloc(ptr, size);
#endif
}

void iot_mem_free(void *ptr)
{
    if (ptr == NULL) {
        return;
    }
#if IOT_USE_MEM_STATS
    {
        iot_mem_hdr_t *hdr =
            (iot_mem_hdr_t *)((uint8_t *)ptr - IOT_MEM_HDR_SIZE);

        iot_mem_account_sub(hdr->h.tid, hdr->h.size);
        port_mem_free(hdr);
    }
#else
    port_mem_free(ptr);
#endif
}

int iot_mem_get_info(iot_mem_info_t *out)
{
    if (out == NULL) {
        return -1;
    }
#if defined(IOT_MEM_PORT)
    if (port_mem_info(out) != 0) {
        return -1;
    }
#else
    /* libc 分配器不提供容量信息，但内核仍能给出已用量 */
    out->total_bytes = 0U;
    out->used_bytes = 0U;
    out->free_bytes = 0U;
    out->max_free_bytes = 0U;
#endif
#if IOT_USE_MEM_STATS
    out->untracked_bytes = s_used_untracked;
#  if !defined(IOT_MEM_PORT)
    out->used_bytes = s_used_total;
#  endif
#else
    out->untracked_bytes = 0U;
#endif
    return 0;
}
