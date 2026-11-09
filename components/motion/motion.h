#ifndef MOTION_H
#define MOTION_H

/*
 * MotionScript 解释器对外门面（Facade）。
 *
 * 职责：向应用层提供“初始化 -> 加载脚本 -> 启动/暂停/恢复/停止 -> 周期
 * 调度 -> 查询状态与错误”的完整接口，内部聚合设备层、流程索引、
 * 主 VM 与 JOB 任务层。
 *
 * 执行模型：motion_poll() 需由主循环周期性调用，它先推进主 VM，
 * 再推进所有 JOB；解释器本身不创建线程、不阻塞。
 *
 * 静态资源：脚本文本保存在 MOTION_MAX_PROGRAM 字节的静态缓冲区，
 * 主 VM 与主数据源也是静态对象，因此同一时刻仅支持一份主程序。
 */

#include "motion_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 初始化解释器、设备层与任务层。
 * 返回：0（总是成功）。
 * 注意：会清空已加载的脚本与全部运行状态。
 */
int motion_init(void);

/*
 * 加载脚本（内部复制到静态缓冲区）。
 * 参数：text 以 '\0' 结尾的脚本，长度必须小于 MOTION_MAX_PROGRAM。
 * 返回：0 成功；MOTION_ERR_PARAMETER 参数为空；
 *       MOTION_ERR_MEMORY 脚本文本过长；其余为流程构建或 JOB 注册错误。
 * 注意：加载成功后可重复调用 motion_start() 重新开始。
 */
int motion_load(const char *text);

/* 启动主程序；未加载脚本时返回 MOTION_ERR_PARAMETER */
int motion_start(void);

/* 暂停主程序（对 RUNNING/WAITING 生效） */
int motion_pause(void);

/* 恢复主程序（对 PAUSED 生效） */
int motion_resume(void);

/* 停止主程序：置为 COMPLETED */
int motion_stop(void);

/* 中止主程序：置为 ABORTED */
int motion_abort(void);

/* 调度一次：主 VM + 全部 JOB（需在主循环中周期调用） */
void motion_poll(void);

/* 查询主程序当前状态 */
motion_state_e motion_state(void);

/*
 * 获取最近一次错误信息。
 * 返回：指向静态 VM 内部错误结构的只读指针（始终非 NULL）。
 */
const motion_error_t *motion_get_error(void);

#ifdef __cplusplus
}
#endif

#endif /* MOTION_H */
