#ifndef SCHEDULER_H
#define SCHEDULER_H

/*
 * 定时任务调度器（标准 Linux crontab 语法）。
 *
 * 表达式支持 5 个字段：分 时 日 月 周
 *   分：0-59，时：0-23，日：1-31，月：1-12，周：0-7（0 和 7 均为周日）
 * 每个字段支持：
 *   *        任意值
 *   n        单个值
 *   a-b      范围
 *   a,b,c    列表
 *   * / n    步进（写作星号斜线，中间无空格）
 *   a-b/n    范围步进
 *
 * 另外支持标准宏：
 *   @yearly / @annually   0 0 1 1 *
 *   @monthly              0 0 1 * *
 *   @weekly               0 0 * * 0
 *   @daily / @midnight    0 0 * * *
 *   @hourly               0 * * * *
 *   @reboot               首次 process 时执行一次
 *
 * 日与周同时被限定时，按 cron 规则二者为“或”关系；任一为 * 时为“与”关系。
 * 匹配粒度为分钟，同一分钟内重复调用 process 只触发一次。
 *
 * 资源约束：任务表为固定大小静态数组（SCHEDULER_MAX_ENTRIES），不支持动态扩容；
 * 回调在 scheduler_process 的调用上下文中同步执行，应保持短小、避免阻塞。
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "datetime.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 任务回调：user 为 scheduler_add 传入的用户上下文 */
typedef void (*scheduler_fn)(void *user);

/* 初始化调度器：清空所有任务槽位。返回 0。 */
int scheduler_init(void);

/*
 * 添加一个定时任务。
 * 参数：crontab 表达式（5 字段或 @宏）；callback 触发回调；user 回调上下文；
 *       id 输出任务标识（可为 NULL）。
 * 返回：0 成功；-1 参数非法、表达式解析失败或任务表已满。
 */
int scheduler_add(const char *crontab, scheduler_fn callback,
                  void *user, uint32_t *id);

/*
 * 删除任务。
 * 参数：id 由 scheduler_add 返回的任务标识。
 * 返回：0 成功；-1 标识无效或任务不存在。
 */
int scheduler_remove(uint32_t id);

/* 由主循环传入当前时间，检查并触发到期任务 */
int scheduler_process(const datetime_t *now);

#ifdef __cplusplus
}
#endif

#endif /* SCHEDULER_H */
