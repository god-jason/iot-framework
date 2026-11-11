#ifndef IOT_MEM_H
#define IOT_MEM_H

/*
 * 内核内存分配接口（实现见 src/iot_mem.c）。
 *
 * 后端选择（编译期）：
 *   - 默认：直接使用 libc 的 malloc / realloc / free（PC / AVR）；
 *   - 定义 IOT_MEM_PORT 时：由平台提供 port_mem_malloc / port_mem_realloc /
 *     port_mem_free / port_mem_info（STM32 用 vendor/tlsf 管理静态堆）。
 *
 * 任务归属统计（IOT_USE_MEM_STATS，默认开）：
 *   在用户指针前放一个指针对齐的头，记录字节数与所属任务 tid；
 *   释放时归还到对应任务，可被 ps / state 读取（每任务 mem_used / mem_peak）。
 *
 * 约定：
 *   - 分配失败返回 NULL，调用者必须判空；
 *   - iot_mem_free(NULL) 为空操作；
 *   - 本接口不加锁，多任务环境由调用者保证串行访问。
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 分配 size 字节；size 为 0 时行为由后端决定（可能返回 NULL） */
void *iot_mem_malloc(size_t size);

/* 分配 count * size 字节并清零；溢出或失败返回 NULL */
void *iot_mem_calloc(size_t count, size_t size);

/* 调整已分配块；ptr 为 NULL 等价 iot_mem_malloc，size 为 0 等价 iot_mem_free */
void *iot_mem_realloc(void *ptr, size_t size);

/* 释放由 iot_mem_malloc/calloc/realloc 返回的内存 */
void iot_mem_free(void *ptr);

/* 堆使用情况，供状态显示/统计；字段由后端尽力提供 */
typedef struct iot_mem_info {
    uint32_t total_bytes;       /* 堆总容量；0 = 该后端不提供容量 */
    uint32_t used_bytes;        /* 已分配的用户字节数（近似，不含分配器开销） */
    uint32_t free_bytes;        /* 剩余可用字节数 */
    uint32_t max_free_bytes;    /* 最大连续空闲块；0 = 未知 */
    uint32_t untracked_bytes;   /* 非任务上下文（tid=0）分配的用户字节 */
} iot_mem_info_t;

/*
 * 查询堆使用情况。
 * 返回 0 成功（后端不支持统计时 total_bytes 为 0）；out 为 NULL 返回 -1。
 */
int iot_mem_get_info(iot_mem_info_t *out);

#ifdef __cplusplus
}
#endif

#endif /* IOT_MEM_H */
