/*
 * iot_sbuf.c - 流式字节缓冲
 *
 * 环形结构：head 为读位置、tail 为写位置、count 单独维护；
 * 因此不需要区分「空/满」，且容量不必是 2 的幂。
 *
 * 阻塞策略：
 *   - 有数据/有空间：立即完成本次可用字节数并返回，不等待凑满 len；
 *   - 完全无数据/无空间：挂入对应等待队列，按 timeout 等待；
 *   - 每次状态变化唤醒对方全部等待者，避免多个读者/写者互相饿死。
 *
 * 中断：FromISR 版本在 iot_isr.c 中，复用本文件的非阻塞路径。
 */

#include "iot.h"
#include "iot_port.h"
#include "iot_mem.h"

#include <string.h>

#if IOT_USE_SBUF

/* 唤醒队列中的全部任务（拥塞解除后让所有等待者重试） */
static void iot_sbuf_wake_all(iot_wait_queue_t *queue)
{
    iot_task_t *task;

    while ((task = iot_wait_queue_pop_highest(queue)) != NULL) {
        iot_task_wake(task, IOT_OK);
    }
}

/* 写入环形缓冲（调用者已保证 len <= 剩余空间） */
static void iot_sbuf_put(iot_sbuf_t *sb, const uint8_t *src, uint32_t len)
{
    uint32_t first = sb->size - sb->tail;

    if (first > len) {
        first = len;
    }
    memcpy(sb->buffer + sb->tail, src, first);
    if (len > first) {
        memcpy(sb->buffer, src + first, len - first);
    }
    sb->tail = (sb->tail + len) % sb->size;
    sb->count += len;
}

/* 从环形缓冲读出（调用者已保证 len <= 已缓存字节数） */
static void iot_sbuf_get(iot_sbuf_t *sb, uint8_t *dst, uint32_t len)
{
    uint32_t first = sb->size - sb->head;

    if (first > len) {
        first = len;
    }
    memcpy(dst, sb->buffer + sb->head, first);
    if (len > first) {
        memcpy(dst + first, sb->buffer, len - first);
    }
    sb->head = (sb->head + len) % sb->size;
    sb->count -= len;
}

/* 初始化流式缓冲：绑定外部缓冲区并清空读写位置与等待队列。 */
int iot_sbuf_init(iot_sbuf_t *sb, void *buffer, uint32_t size)
{
    if (sb == NULL || buffer == NULL || size == 0U) {
        LOG_E(IOT_TAG, "sbuf_init: invalid param");
        return IOT_INVALID_PARAM;
    }

    sb->buffer = (uint8_t *)buffer;
    sb->buffer_owned = false;
    sb->size = size;
    sb->head = 0U;
    sb->tail = 0U;
    sb->count = 0U;
    iot_wait_queue_init(&sb->send_wait);
    iot_wait_queue_init(&sb->recv_wait);
    sb->initialized = true;
    return IOT_OK;
}

/*
 * 写入最多 len 字节：有空间立即写入当前可容纳的部分并返回；
 * 完全无空间时按 timeout 阻塞，唤醒后重试。
 */
int iot_sbuf_write(iot_sbuf_t *sb, const void *data, uint32_t len,
                   uint32_t timeout)
{
    const uint8_t *src = (const uint8_t *)data;
    uint32_t deadline;

    if (!IOT_OBJ_VALID(sb) || data == NULL || len == 0U) {
        return IOT_INVALID_PARAM;
    }
    if (iot_in_isr() && timeout != IOT_TIMEOUT_NONE) {
        IOT_ASSERT(!iot_in_isr());
        return IOT_FORBIDDEN_IN_ISR;
    }

    deadline = iot_tick_get() + timeout;

    for (;;) {
        uint32_t space;
        uint32_t n;

        iot_port_critical_enter();
        space = sb->size - sb->count;
        if (space > 0U) {
            n = (len < space) ? len : space;
            iot_sbuf_put(sb, src, n);
            iot_sbuf_wake_all(&sb->recv_wait);
            iot_port_critical_exit();
            return (int)n;
        }
        iot_port_critical_exit();

        if (timeout == IOT_TIMEOUT_NONE) {
            return 0;
        }

        {
            uint32_t now = iot_tick_get();
            uint32_t wait;

            if (timeout == IOT_TIMEOUT_FOREVER) {
                wait = IOT_TIMEOUT_FOREVER;
            } else {
                if (!IOT_TICK_AFTER(deadline, now)) {
                    return 0;
                }
                wait = deadline - now;
            }
            if (iot_block_current(&sb->send_wait, wait) != IOT_OK) {
                return 0;
            }
        }
    }
}

/*
 * 读取最多 len 字节：有数据立即返回当前可用部分；
 * 完全无数据时按 timeout 阻塞，唤醒后重试。
 */
int iot_sbuf_read(iot_sbuf_t *sb, void *data, uint32_t len, uint32_t timeout)
{
    uint8_t *dst = (uint8_t *)data;
    uint32_t deadline;

    if (!IOT_OBJ_VALID(sb) || data == NULL || len == 0U) {
        return IOT_INVALID_PARAM;
    }
    if (iot_in_isr() && timeout != IOT_TIMEOUT_NONE) {
        IOT_ASSERT(!iot_in_isr());
        return IOT_FORBIDDEN_IN_ISR;
    }

    deadline = iot_tick_get() + timeout;

    for (;;) {
        uint32_t avail;
        uint32_t n;

        iot_port_critical_enter();
        avail = sb->count;
        if (avail > 0U) {
            n = (len < avail) ? len : avail;
            iot_sbuf_get(sb, dst, n);
            iot_sbuf_wake_all(&sb->send_wait);
            iot_port_critical_exit();
            return (int)n;
        }
        iot_port_critical_exit();

        if (timeout == IOT_TIMEOUT_NONE) {
            return 0;
        }

        {
            uint32_t now = iot_tick_get();
            uint32_t wait;

            if (timeout == IOT_TIMEOUT_FOREVER) {
                wait = IOT_TIMEOUT_FOREVER;
            } else {
                if (!IOT_TICK_AFTER(deadline, now)) {
                    return 0;
                }
                wait = deadline - now;
            }
            if (iot_block_current(&sb->recv_wait, wait) != IOT_OK) {
                return 0;
            }
        }
    }
}

/* 返回当前已缓存字节数；对象无效时返回 0。 */
uint32_t iot_sbuf_count(const iot_sbuf_t *sb)
{
    return (sb != NULL && sb->initialized) ? sb->count : 0U;
}

/* 返回剩余可写空间（字节）；对象无效时返回 0。 */
uint32_t iot_sbuf_space(const iot_sbuf_t *sb)
{
    return (sb != NULL && sb->initialized) ? (sb->size - sb->count) : 0U;
}

/* 清空缓冲（丢弃未读数据），并唤醒所有等待空间的写者。 */
int iot_sbuf_reset(iot_sbuf_t *sb)
{
    if (!IOT_OBJ_VALID(sb)) {
        return IOT_INVALID_PARAM;
    }

    iot_port_critical_enter();
    sb->head = 0U;
    sb->tail = 0U;
    sb->count = 0U;
    iot_sbuf_wake_all(&sb->send_wait);
    iot_port_critical_exit();
    return IOT_OK;
}

/* 动态创建流式缓冲：对象与 size 字节缓冲区均由 iot_mem_* 分配。 */
iot_sbuf_t *iot_sbuf_create(uint32_t size)
{
    iot_sbuf_t *sb;

    if (size == 0U) {
        return NULL;
    }

    sb = (iot_sbuf_t *)iot_mem_calloc(1U, sizeof(*sb));
    if (sb == NULL) {
        LOG_E(IOT_TAG, "sbuf_create: no memory for object");
        return NULL;
    }
    sb->buffer = (uint8_t *)iot_mem_malloc(size);
    if (sb->buffer == NULL) {
        LOG_E(IOT_TAG, "sbuf_create: no memory for buffer");
        iot_mem_free(sb);
        return NULL;
    }

    if (iot_sbuf_init(sb, sb->buffer, size) != IOT_OK) {
        iot_mem_free(sb->buffer);
        iot_mem_free(sb);
        return NULL;
    }
    sb->buffer_owned = true;
    return sb;
}

/*
 * 销毁动态流式缓冲：仍有读写等待者时返回 IOT_BUSY；
 * 成功时释放缓冲区（若是动态分配）与对象内存。
 */
int iot_sbuf_destroy(iot_sbuf_t *sb)
{
    if (!IOT_OBJ_VALID(sb)) {
        return IOT_INVALID_PARAM;
    }

    iot_port_critical_enter();
    if (!iot_wait_queue_empty(&sb->send_wait) ||
        !iot_wait_queue_empty(&sb->recv_wait)) {
        iot_port_critical_exit();
        LOG_W(IOT_TAG, "sbuf_destroy: still has waiters");
        return IOT_BUSY;
    }
    sb->initialized = false;
    iot_port_critical_exit();

    if (sb->buffer_owned) {
        iot_mem_free(sb->buffer);
    }
    iot_mem_free(sb);
    return IOT_OK;
}

#endif /* IOT_USE_SBUF */
