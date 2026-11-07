#ifndef PORT_MEM_H
#define PORT_MEM_H

/*
 * PORT 内存分配接口。
 *
 * 目的：为上层模块屏蔽具体分配器实现，便于替换后端。
 *   - 模拟器（ports/simulator）：使用标准库 malloc/free；
 *   - STM32（ports/stm32*）：使用 vendor/tlsf 管理一块静态堆；
 *   - AVR（ports/avr）：使用 libc 分配器。
 *
 * 约定：
 *   - 分配失败返回 NULL，调用者必须判空；
 *   - port_free(NULL) 为空操作；
 *   - 本接口不加锁，多任务环境由调用者保证串行访问。
 */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 分配 size 字节；size 为 0 时行为由后端决定（可能返回 NULL） */
void *port_malloc(size_t size);

/* 分配 count * size 字节并清零；溢出或失败返回 NULL */
void *port_calloc(size_t count, size_t size);

/* 调整已分配块；ptr 为 NULL 等价 port_malloc，size 为 0 等价 port_free */
void *port_realloc(void *ptr, size_t size);

/* 释放由 port_malloc/calloc/realloc 返回的内存 */
void port_free(void *ptr);

#ifdef __cplusplus
}
#endif

#endif /* PORT_MEM_H */
