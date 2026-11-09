#ifndef ALOG_H
#define ALOG_H

/*
 * alog.h - 异步日志环形缓冲
 *
 * 生产者（含中断）只做字节拷贝，消费者（低优先级日志任务）负责输出，
 * 避免 printf 阻塞控制环。缓冲满时覆盖最旧数据并记录丢弃计数。
 *
 * 无动态内存，缓冲区由调用者提供。
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char *buf;          /* 外部缓冲区 */
    uint32_t size;      /* 容量（字节） */
    uint32_t head;      /* 最旧字节位置 */
    uint32_t count;     /* 当前有效字节数 */
    uint32_t dropped;   /* 因缓冲满被覆盖的字节数 */
} alog_t;

/*
 * 初始化日志缓冲。
 * 返回：0 成功；-1 参数非法。
 */
int alog_init(alog_t *log, char *buf, uint32_t size);

/* 清空缓冲（不清除丢弃计数） */
void alog_clear(alog_t *log);

/*
 * 写入字节；空间不足时覆盖最旧数据。
 * 返回：实际写入的字节数；参数非法返回 0。
 */
uint32_t alog_write(alog_t *log, const char *data, uint32_t len);

/*
 * 读取最旧的字节到 out。
 * 返回：实际读取的字节数；参数非法返回 0。
 */
uint32_t alog_read(alog_t *log, char *out, uint32_t max);

/* 当前缓存字节数 / 累计丢弃字节数 */
uint32_t alog_count(const alog_t *log);
uint32_t alog_dropped(const alog_t *log);

#ifdef __cplusplus
}
#endif

#endif /* ALOG_H */
