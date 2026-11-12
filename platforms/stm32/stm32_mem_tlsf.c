/*
 * stm32_mem_tlsf.c - STM32 内存后端（TLSF 静态堆）
 *
 * 本文件只实现 port_mem_* 原语，供 src/ros_mem.c 的统一接口调用：
 *   ros_mem_malloc/free/realloc 负责任务统计与 calloc/边界处理，
 *   这里只做纯粹的块分配与堆容量上报。
 *
 * 堆大小可通过编译选项 ROS_MEM_HEAP_SIZE 覆盖；
 * 首次分配时惰性创建 TLSF 控制结构与初始内存池。
 *
 * port_mem_info() 的“已用”是各块 tlsf_block_size() 之和（含 ros_mem 的
 * 统计头），因此百分比略低于真实占用；用于状态显示足够。
 */
#include "ros_mem.h"

#include <stdint.h>
#include <string.h>

#include "tlsf.h"

#ifndef ROS_MEM_HEAP_SIZE
#define ROS_MEM_HEAP_SIZE (32U * 1024U)
#endif

/* 以 uint64_t 数组保证堆区按 8 字节对齐 */
static uint64_t s_heap[(ROS_MEM_HEAP_SIZE + 7U) / 8U];
static tlsf_t s_tlsf = NULL;
static size_t s_used;

static void ros_mem_ensure(void)
{
    if (s_tlsf == NULL) {
        s_tlsf = tlsf_create_with_pool(s_heap, sizeof(s_heap));
    }
}

void *port_mem_malloc(size_t size)
{
    void *ptr;

    ros_mem_ensure();
    ptr = (s_tlsf != NULL) ? tlsf_malloc(s_tlsf, size) : NULL;
    if (ptr != NULL) {
        s_used += tlsf_block_size(ptr);
    }
    return ptr;
}

void *port_mem_realloc(void *ptr, size_t size)
{
    void *out;
    size_t old_size = 0U;

    ros_mem_ensure();
    if (s_tlsf == NULL) {
        return NULL;
    }
    if (ptr != NULL) {
        old_size = tlsf_block_size(ptr);
    }
    out = tlsf_realloc(s_tlsf, ptr, size);
    if (out != NULL) {
        s_used = s_used - old_size + tlsf_block_size(out);
    }
    return out;
}

void port_mem_free(void *ptr)
{
    if (s_tlsf == NULL) {
        return;
    }
    if (ptr != NULL) {
        size_t size = tlsf_block_size(ptr);

        s_used = (s_used >= size) ? (s_used - size) : 0U;
    }
    tlsf_free(s_tlsf, ptr);
}

int port_mem_info(ros_mem_info_t *out)
{
    uint32_t total;

    if (out == NULL) {
        return -1;
    }

    total = (uint32_t)sizeof(s_heap);
    out->total_bytes = total;
    out->used_bytes = (uint32_t)((s_used < total) ? s_used : total);
    out->free_bytes = (total >= out->used_bytes) ? (total - out->used_bytes) : 0U;
    out->max_free_bytes = 0U;       /* TLSF 未暴露空闲链表统计 */
    return 0;
}
