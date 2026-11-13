/*
 * ros_sbuf.c - 流式字节缓冲
 *
 * 环形结构：head 为读位置、tail 为写位置、count 单独维护；
 * 因此不需要区分「空/满」，且容量不必是 2 的幂。
 *
 * 阻塞策略：
 *   - 有数据/有空间：立即完成本次可用字节数并返回，不等待凑满 len；
 *   - 完全无数据/无空间：挂入对应等待队列，按 timeout 等待；
 *   - 每次状态变化唤醒对方全部等待者，避免多个读者/写者互相饿死。
 *
 * 中断：FromISR 版本在 ros_isr.c 中，复用本文件的非阻塞路径。
 */

#include "ros.h"
#include "common.h"
#include "ros_port.h"
#include "ros_mem.h"

#include <string.h>

#if ROS_USE_SBUF

/* 写入环形缓冲（调用者已保证 len <= 剩余空间） */
static void ros_sbuf_put(ros_sbuf_t *sb, const uint8_t *src, uint32_t len)
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
static void ros_sbuf_get(ros_sbuf_t *sb, uint8_t *dst, uint32_t len)
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
int ros_sbuf_init(ros_sbuf_t *sb, void *buffer, uint32_t size)
{
    if (sb == NULL || buffer == NULL || size == 0U) {
        LOG_E(ROS_TAG, "sbuf_init: invalid param");
        return ROS_INVALID_PARAM;
    }

    sb->buffer = (uint8_t *)buffer;
    sb->buffer_owned = false;
    sb->size = size;
    sb->head = 0U;
    sb->tail = 0U;
    sb->count = 0U;
    ros_wait_queue_init(&sb->send_wait);
    ros_wait_queue_init(&sb->recv_wait);
    sb->initialized = true;
    return ROS_OK;
}

/*
 * 写入最多 len 字节：有空间立即写入当前可容纳的部分并返回；
 * 完全无空间时按 timeout 阻塞，唤醒后重试。
 */
int ros_sbuf_write(ros_sbuf_t *sb, const void *data, uint32_t len,
                   uint32_t timeout)
{
    const uint8_t *src = (const uint8_t *)data;
    uint32_t deadline;

    if (!ROS_OBJ_VALID(sb) || data == NULL || len == 0U) {
        return ROS_INVALID_PARAM;
    }
    if (!ros_wait_blockable(timeout)) {
        return ROS_FORBIDDEN_IN_ISR;
    }

    deadline = ros_tick_get() + timeout;

    for (;;) {
        uint32_t space;
        uint32_t n;

        ros_port_critical_enter();
        space = sb->size - sb->count;
        if (space > 0U) {
            n = (len < space) ? len : space;
            ros_sbuf_put(sb, src, n);
            ros_wait_queue_wake_all(&sb->recv_wait);
            ros_port_critical_exit();
            ROS_TRACE_OBJ(ROS_EVT_SBUF_WRITE, (uint32_t)(uintptr_t)sb, n);
            return (int)n;
        }
        ros_port_critical_exit();

        if (ros_wait_retry(&sb->send_wait, deadline, timeout) != ROS_OK) {
            return 0;
        }
    }
}

/*
 * 读取最多 len 字节：有数据立即返回当前可用部分；
 * 完全无数据时按 timeout 阻塞，唤醒后重试。
 */
int ros_sbuf_read(ros_sbuf_t *sb, void *data, uint32_t len, uint32_t timeout)
{
    uint8_t *dst = (uint8_t *)data;
    uint32_t deadline;

    if (!ROS_OBJ_VALID(sb) || data == NULL || len == 0U) {
        return ROS_INVALID_PARAM;
    }
    if (!ros_wait_blockable(timeout)) {
        return ROS_FORBIDDEN_IN_ISR;
    }

    deadline = ros_tick_get() + timeout;

    for (;;) {
        uint32_t avail;
        uint32_t n;

        ros_port_critical_enter();
        avail = sb->count;
        if (avail > 0U) {
            n = (len < avail) ? len : avail;
            ros_sbuf_get(sb, dst, n);
            ros_wait_queue_wake_all(&sb->send_wait);
            ros_port_critical_exit();
            ROS_TRACE_OBJ(ROS_EVT_SBUF_READ, (uint32_t)(uintptr_t)sb, n);
            return (int)n;
        }
        ros_port_critical_exit();

        if (ros_wait_retry(&sb->recv_wait, deadline, timeout) != ROS_OK) {
            return 0;
        }
    }
}

/* 返回当前已缓存字节数；对象无效时返回 0。 */
uint32_t ros_sbuf_count(const ros_sbuf_t *sb)
{
    return (sb != NULL && sb->initialized) ? sb->count : 0U;
}

/* 返回剩余可写空间（字节）；对象无效时返回 0。 */
uint32_t ros_sbuf_space(const ros_sbuf_t *sb)
{
    return (sb != NULL && sb->initialized) ? (sb->size - sb->count) : 0U;
}

/* 清空缓冲（丢弃未读数据），并唤醒所有等待空间的写者。 */
int ros_sbuf_reset(ros_sbuf_t *sb)
{
    if (!ROS_OBJ_VALID(sb)) {
        return ROS_INVALID_PARAM;
    }

    ros_port_critical_enter();
    sb->head = 0U;
    sb->tail = 0U;
    sb->count = 0U;
    ros_wait_queue_wake_all(&sb->send_wait);
    ros_port_critical_exit();
    return ROS_OK;
}

/* 动态创建流式缓冲：对象与 size 字节缓冲区均由 ros_mem_* 分配。 */
ros_sbuf_t *ros_sbuf_create(uint32_t size)
{
    ros_sbuf_t *sb;

    if (size == 0U) {
        return NULL;
    }

    sb = (ros_sbuf_t *)ros_mem_calloc(1U, sizeof(*sb));
    if (sb == NULL) {
        LOG_E(ROS_TAG, "sbuf_create: no memory for object");
        return NULL;
    }
    sb->buffer = (uint8_t *)ros_mem_malloc(size);
    if (sb->buffer == NULL) {
        LOG_E(ROS_TAG, "sbuf_create: no memory for buffer");
        ros_mem_free(sb);
        return NULL;
    }

    if (ros_sbuf_init(sb, sb->buffer, size) != ROS_OK) {
        ros_mem_free(sb->buffer);
        ros_mem_free(sb);
        return NULL;
    }
    sb->buffer_owned = true;
    return sb;
}

/*
 * 销毁动态流式缓冲：仍有读写等待者时返回 ROS_BUSY；
 * 成功时释放缓冲区（若是动态分配）与对象内存。
 */
int ros_sbuf_destroy(ros_sbuf_t *sb)
{
    if (!ROS_OBJ_VALID(sb)) {
        return ROS_INVALID_PARAM;
    }

    if (ros_obj_destroy_begin(&sb->initialized,
                              &sb->send_wait, &sb->recv_wait) != ROS_OK) {
        LOG_W(ROS_TAG, "sbuf_destroy: still has waiters");
        return ROS_BUSY;
    }

    if (sb->buffer_owned) {
        ros_mem_free(sb->buffer);
    }
    ros_mem_free(sb);
    return ROS_OK;
}

#endif /* ROS_USE_SBUF */
