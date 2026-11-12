/*
 * ros_msgq.c - 变长消息队列
 *
 * 数据结构：环形字节缓冲 + 已用字节计数 used。
 *   每条消息 = [uint32_t len][len 字节 payload]，头与负载都可能跨越缓冲末尾回绕。
 *   判断能否发送只看 size - used 是否足够，不需要区分空/满。
 *
 * 阻塞策略：
 *   - 空间不足时整条消息不写，挂入 send_wait；
 *   - 队列为空时挂入 recv_wait；
 *   - 每次状态变化唤醒对方全部等待者，避免多个等待者互相饿死。
 *
 * 中断：FromISR 版本在 ros_isr.c 中，复用非阻塞路径。
 */

#include "ros.h"
#include "ros_port.h"
#include "ros_mem.h"

#include <string.h>

#if ROS_USE_MSGQ

/* 向环形缓冲写入 len 字节（调用者已保证空间足够） */
static void ros_msgq_put(ros_msgq_t *mq, const void *src, uint32_t len)
{
    const uint8_t *p = (const uint8_t *)src;
    uint32_t first = mq->size - mq->tail;

    if (first > len) {
        first = len;
    }
    memcpy(mq->buffer + mq->tail, p, first);
    if (len > first) {
        memcpy(mq->buffer, p + first, len - first);
    }
    mq->tail = (mq->tail + len) % mq->size;
    mq->used += len;
}

/* 从队首偏移 offset 处读取 len 字节但不移动读位置 */
static void ros_msgq_peek_at(const ros_msgq_t *mq, uint32_t offset,
                             void *dst, uint32_t len)
{
    uint8_t *p = (uint8_t *)dst;
    uint32_t pos = (mq->head + offset) % mq->size;
    uint32_t first = mq->size - pos;

    if (first > len) {
        first = len;
    }
    memcpy(p, mq->buffer + pos, first);
    if (len > first) {
        memcpy(p + first, mq->buffer, len - first);
    }
}

/* 读位置前移 len 字节并减少已用空间 */
static void ros_msgq_skip(ros_msgq_t *mq, uint32_t len)
{
    mq->head = (mq->head + len) % mq->size;
    mq->used -= len;
}

/* 初始化变长消息队列：绑定缓冲区并清空读写位置、计数与等待队列。 */
int ros_msgq_init(ros_msgq_t *mq, void *buffer, uint32_t size)
{
    if (mq == NULL || buffer == NULL || size <= ROS_MSGQ_HDR_SIZE) {
        LOG_E(ROS_TAG, "msgq_init: invalid param");
        return ROS_INVALID_PARAM;
    }

    mq->buffer = (uint8_t *)buffer;
    mq->buffer_owned = false;
    mq->size = size;
    mq->head = 0U;
    mq->tail = 0U;
    mq->used = 0U;
    mq->count = 0U;
    ros_wait_queue_init(&mq->send_wait);
    ros_wait_queue_init(&mq->recv_wait);
    mq->initialized = true;
    return ROS_OK;
}

/*
 * 发送整条变长消息：空间足够时一次性写入「长度头 + 负载」；
 * 空间不足时整条不写，按 timeout 等待（不会产生半条消息）。
 */
int ros_msgq_send(ros_msgq_t *mq, const void *msg, uint32_t len,
                  uint32_t timeout)
{
    uint32_t need;
    uint32_t deadline;
    uint32_t header;

    if (!ROS_OBJ_VALID(mq) || msg == NULL || len == 0U ||
        len > (mq->size - ROS_MSGQ_HDR_SIZE)) {
        return ROS_INVALID_PARAM;
    }
    if (!ros_wait_blockable(timeout)) {
        return ROS_FORBIDDEN_IN_ISR;
    }

    need = len + ROS_MSGQ_HDR_SIZE;
    deadline = ros_tick_get() + timeout;
    header = len;

    for (;;) {
        ros_port_critical_enter();
        if (mq->size - mq->used >= need) {
            /* 头与负载整体写入；空间已提前校验，不会写半条 */
            ros_msgq_put(mq, &header, ROS_MSGQ_HDR_SIZE);
            ros_msgq_put(mq, msg, len);
            mq->count++;
            ros_wait_queue_wake_all(&mq->recv_wait);
            ros_port_critical_exit();
            ROS_TRACE_OBJ(ROS_EVT_MSGQ_SEND, (uint32_t)(uintptr_t)mq, len);
            return ROS_OK;
        }
        ros_port_critical_exit();

        if (ros_wait_retry(&mq->send_wait, deadline, timeout) != ROS_OK) {
            return ROS_TIMEOUT;
        }
    }
}

/*
 * 接收一条消息：先读长度头，缓冲不足时返回 ROS_OVERFLOW 并保留消息；
 * msg 为 NULL 且 max_len 为 0 时只查询队首消息长度。
 */
int ros_msgq_recv(ros_msgq_t *mq, void *msg, uint32_t max_len,
                  uint32_t *out_len, uint32_t timeout)
{
    uint32_t deadline;

    if (!ROS_OBJ_VALID(mq) || (msg == NULL && max_len != 0U)) {
        return ROS_INVALID_PARAM;
    }
    if (!ros_wait_blockable(timeout)) {
        return ROS_FORBIDDEN_IN_ISR;
    }

    deadline = ros_tick_get() + timeout;

    for (;;) {
        uint32_t len = 0U;

        ros_port_critical_enter();
        if (mq->count > 0U && mq->used >= ROS_MSGQ_HDR_SIZE) {
            ros_msgq_peek_at(mq, 0U, &len, ROS_MSGQ_HDR_SIZE);

            if (out_len != NULL) {
                *out_len = len;
            }
            if (len == 0U || len > (mq->size - ROS_MSGQ_HDR_SIZE)) {
                /* 内部数据损坏：拒绝继续，交由断言暴露 */
                ros_port_critical_exit();
                ROS_ASSERT(0);
                return ROS_ERROR;
            }
            if (max_len < len) {
                /* 缓冲不足：保留消息，调用方按 out_len 扩容后重试 */
                ros_port_critical_exit();
                return ROS_OVERFLOW;
            }

            ros_msgq_peek_at(mq, ROS_MSGQ_HDR_SIZE, msg, len);
            ros_msgq_skip(mq, len + ROS_MSGQ_HDR_SIZE);
            mq->count--;
            ros_wait_queue_wake_all(&mq->send_wait);
            ros_port_critical_exit();
            ROS_TRACE_OBJ(ROS_EVT_MSGQ_RECV, (uint32_t)(uintptr_t)mq, len);
            return ROS_OK;
        }
        ros_port_critical_exit();

        if (ros_wait_retry(&mq->recv_wait, deadline, timeout) != ROS_OK) {
            return ROS_TIMEOUT;
        }
    }
}

/* 返回当前排队消息条数；对象无效时返回 0。 */
uint32_t ros_msgq_count(const ros_msgq_t *mq)
{
    return (mq != NULL && mq->initialized) ? mq->count : 0U;
}

/* 返回已占用字节数（含长度头）；对象无效时返回 0。 */
uint32_t ros_msgq_bytes(const ros_msgq_t *mq)
{
    return (mq != NULL && mq->initialized) ? mq->used : 0U;
}

/* 清空队列（丢弃全部消息），并唤醒所有等待空间的发送者。 */
int ros_msgq_reset(ros_msgq_t *mq)
{
    if (!ROS_OBJ_VALID(mq)) {
        return ROS_INVALID_PARAM;
    }

    ros_port_critical_enter();
    mq->head = 0U;
    mq->tail = 0U;
    mq->used = 0U;
    mq->count = 0U;
    ros_wait_queue_wake_all(&mq->send_wait);
    ros_port_critical_exit();
    return ROS_OK;
}

/* 动态创建变长消息队列：对象与 size 字节缓冲区均由 ros_mem_* 分配。 */
ros_msgq_t *ros_msgq_create(uint32_t size)
{
    ros_msgq_t *mq;

    if (size <= ROS_MSGQ_HDR_SIZE) {
        return NULL;
    }

    mq = (ros_msgq_t *)ros_mem_calloc(1U, sizeof(*mq));
    if (mq == NULL) {
        LOG_E(ROS_TAG, "msgq_create: no memory for object");
        return NULL;
    }
    mq->buffer = (uint8_t *)ros_mem_malloc(size);
    if (mq->buffer == NULL) {
        LOG_E(ROS_TAG, "msgq_create: no memory for buffer");
        ros_mem_free(mq);
        return NULL;
    }

    if (ros_msgq_init(mq, mq->buffer, size) != ROS_OK) {
        ros_mem_free(mq->buffer);
        ros_mem_free(mq);
        return NULL;
    }
    mq->buffer_owned = true;
    return mq;
}

/*
 * 销毁动态消息队列：仍有收发等待者时返回 ROS_BUSY；
 * 成功时释放缓冲区（若是动态分配）与对象内存。
 */
int ros_msgq_destroy(ros_msgq_t *mq)
{
    if (!ROS_OBJ_VALID(mq)) {
        return ROS_INVALID_PARAM;
    }

    if (ros_obj_destroy_begin(&mq->initialized,
                              &mq->send_wait, &mq->recv_wait) != ROS_OK) {
        LOG_W(ROS_TAG, "msgq_destroy: still has waiters");
        return ROS_BUSY;
    }

    if (mq->buffer_owned) {
        ros_mem_free(mq->buffer);
    }
    ros_mem_free(mq);
    return ROS_OK;
}

#endif /* ROS_USE_MSGQ */
