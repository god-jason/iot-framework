#ifndef MOTION_TASK_H
#define MOTION_TASK_H

/*
 * JOB 任务层接口。
 *
 * 职责：把脚本中的 JOB 段注册为可独立启停的任务，每个 JOB 拥有自己的
 * 数据源与 VM；同时在启动时对 STEP/PWM 设备做互斥占用，避免多个 JOB
 * 并发操作同一设备。
 *
 * 执行模型：motion_task_poll() 每轮只推进处于 RUNNING/WAITING 的 JOB，
 * 单个 JOB 的一次推进受 VM 的 MOTION_MAX_EXEC_PER_POLL 限制；
 * 任务结束（完成/错误/中止/空闲）时自动释放其占用的设备。
 *
 * 依赖：motion_vm（执行）、motion_source（文本）、motion_parser（扫描
 *       设备引用）、motion_device（设备集合）。
 */

#include "motion_types.h"
#include "motion_vm.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 初始化任务层：清空 JOB 表与设备占用表。
 * 返回：0（总是成功）。
 */
int motion_task_init(void);

/*
 * 注册一个 JOB。
 * 参数：
 *   name         - JOB 名称（内部转大写后比较，最长 MOTION_MAX_JOB_NAME）；
 *   segment      - 段号；
 *   start_offset - JOB 体首行偏移；
 *   end_offset   - JOB 结束偏移；
 *   text         - 完整脚本文本（任务持有其指针，调用方需保证生命周期）。
 * 返回：0 成功；MOTION_ERR_UNSUPPORTED 未初始化；
 *       MOTION_ERR_PARAMETER 名称为空或重名；MOTION_ERR_MEMORY 表已满。
 */
int motion_task_add(const char *name, int segment, uint32_t start_offset,
                    uint32_t end_offset, const char *text);

/*
 * 启动指定 JOB。
 * 返回：0 成功（或已在运行）；MOTION_ERR_PARAMETER 未找到；
 *       MOTION_ERR_RESOURCE_BUSY 设备被其他 JOB 占用；
 *       其余为 VM 启动错误。
 */
int motion_task_start(const char *name);

/*
 * 停止指定 JOB 并释放其设备占用。
 * 返回：0 成功；MOTION_ERR_PARAMETER 未找到。
 */
int motion_task_stop(const char *name);

/* 调度所有 JOB 一次：推进运行中的 VM 并回收已结束任务的设备 */
void motion_task_poll(void);

/*
 * 按名称查找 JOB。
 * 返回：JOB 下标（>=0）；未找到或未初始化返回 -1。
 */
int motion_task_find(const char *name);

/* 查询 JOB 状态；未找到时返回 MOTION_STATE_IDLE */
motion_state_e motion_task_state(const char *name);

/* 返回已注册的 JOB 数量 */
uint8_t motion_task_count(void);

#ifdef __cplusplus
}
#endif

#endif /* MOTION_TASK_H */
