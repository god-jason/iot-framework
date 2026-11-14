/*
 * ringbuf.c - 字节环形缓冲区
 *
 * 模块职责：
 *   提供单生产者/单消费者的字节流缓冲，常用于 UART DMA 接收、AT 命令
 *   拼接、日志缓存等场景。存储由调用者提供，模块不分配内存。
 *
 * 数据流与依赖：
 *   read_pos / write_pos 为单调递增的字节计数（不做取模保存），
 *   实际下标 = 位置 % capacity；缓冲区中有效数据量 = write_pos - read_pos。
 *   由于采用无符号整数减法，即使计数回绕到 0，差值仍等于当前占用长度。
 *   仅依赖 标准头文件。
 *
 * 边界约束：
 *   - capacity 可以为任意正整数（不要求 2 的幂），但读写都需做取模运算。
 *   - 写满时只写入能容纳的部分，返回实际写入字节数（不覆盖未读数据）。
 *   - 读/写/peek 都按“请求量与可用量取较小值”处理，不做部分失败。
 *   - read_pos/write_pos 声明为 volatile 仅防止编译器优化，并不能保证
 *     多核/中断并发访问的原子性，跨上下文使用仍需关中断或加锁。
 */

#include "ringbuf.h"

/*
 * 初始化环形缓冲区。
 * 参数：rb - 缓冲区对象；buffer - 外部存储；size - 容量（字节，> 0）
 * 返回：0 成功；-1 参数非法
 * 注意：不初始化存储内容，仅把读写位置归零。
 */
int ringbuf_init(ringbuf_t *rb, void *buffer, uint32_t size)
{
    if (rb == NULL || buffer == NULL || size == 0) {
        return -1;
    }
    rb->buffer = (uint8_t *)buffer;
    rb->capacity = size;
    rb->read_pos = 0;
    rb->write_pos = 0;
    return 0;
}

/*
 * 写入数据。
 * 参数：rb - 缓冲区；data - 源数据；len - 请求写入字节数
 * 返回：实际写入的字节数（0 表示参数非法或缓冲区已满）
 * 注意：空间不足时只写入前 free 个字节，剩余部分丢弃；
 *       写入过程中 write_pos 最后才更新，单生产者场景下不会提前暴露数据。
 */
uint32_t ringbuf_write(ringbuf_t *rb, const void *data, uint32_t len)
{
    const uint8_t *src = (const uint8_t *)data;
    uint32_t avail, free, n, i;

    if (rb == NULL || data == NULL || len == 0) {
        return 0;
    }
    free = ringbuf_free(rb);
    n = (len < free) ? len : free;   /* 取请求量与空闲量的较小值 */

    for (i = 0; i < n; i++) {
        uint32_t idx = (rb->write_pos + i) % rb->capacity;   /* 环形回绕 */
        rb->buffer[idx] = src[i];
    }
    avail = ringbuf_available(rb);
    (void)avail;   /* 写入前的占用量仅用于调试观察，此处不参与计算 */
    rb->write_pos += n;
    return n;
}

/*
 * 读出并移除数据。
 * 参数：rb - 缓冲区；data - 目标缓冲（容量至少 len）；len - 请求读取字节数
 * 返回：实际读取的字节数（0 表示参数非法或无数据）
 * 注意：可读数据不足时只读出可用部分；read_pos 在拷贝完成后一次性前移。
 */
uint32_t ringbuf_read(ringbuf_t *rb, void *data, uint32_t len)
{
    uint8_t *dst = (uint8_t *)data;
    uint32_t avail, n, i;

    if (rb == NULL || data == NULL || len == 0) {
        return 0;
    }
    avail = ringbuf_available(rb);
    n = (len < avail) ? len : avail;

    for (i = 0; i < n; i++) {
        uint32_t idx = (rb->read_pos + i) % rb->capacity;
        dst[i] = rb->buffer[idx];
    }
    rb->read_pos += n;
    return n;
}

/*
 * 窥视数据（读取但不移除）。
 * 参数：rb - 缓冲区；data - 目标缓冲（容量至少 len）；len - 请求字节数
 * 返回：实际拷贝的字节数（0 表示参数非法或无数据）
 * 注意：read_pos 保持不变，可与 ringbuf_skip() 配合实现“确认后消费”。
 */
uint32_t ringbuf_peek(ringbuf_t *rb, void *data, uint32_t len)
{
    uint8_t *dst = (uint8_t *)data;
    uint32_t avail, n, i;

    if (rb == NULL || data == NULL || len == 0) {
        return 0;
    }
    avail = ringbuf_available(rb);
    n = (len < avail) ? len : avail;

    for (i = 0; i < n; i++) {
        uint32_t idx = (rb->read_pos + i) % rb->capacity;
        dst[i] = rb->buffer[idx];
    }
    return n;
}

/*
 * 跳过（丢弃）若干已读位置的数据。
 * 参数：rb - 缓冲区；len - 期望跳过的字节数
 * 返回：实际跳过的字节数（不超过当前可读量）
 * 注意：仅前移 read_pos，不访问缓冲区内容。
 */
uint32_t ringbuf_skip(ringbuf_t *rb, uint32_t len)
{
    uint32_t avail, n;

    if (rb == NULL || len == 0) {
        return 0;
    }
    avail = ringbuf_available(rb);
    n = (len < avail) ? len : avail;
    rb->read_pos += n;
    return n;
}
