#include "motion.h"
#include "motion_device.h"
#include "motion_error.h"
#include "motion_flow.h"
#include "motion_source.h"
#include "motion_task.h"
#include "motion_vm.h"
#include "iot_mem.h"

#include <string.h>

/*
 * 解释器门面实现。
 *
 * 执行模型：
 *   - 持有脚本文本缓冲区 s_program（首次初始化时分配）、字符串数据源
 *     s_source 与主 VM s_vm；
 *   - motion_load() 先复制文本，再调用 motion_flow_build() 建立流程索引，
 *     随后按索引把每个 JOB 注册到任务层，最后为主程序绑定数据源；
 *   - motion_poll() 依次推进主 VM 与全部 JOB，单次调用不会阻塞。
 *
 * 数据流/依赖：
 *   motion_flow（索引/表达式）<- motion_parser；motion_vm（执行）<- 
 *   motion_executor -> motion_device；JOB 由 motion_task 独立调度。
 *
 * 静态缓冲与边界：
 *   - 脚本长度必须 < MOTION_MAX_PROGRAM，否则返回 MOTION_ERR_MEMORY；
 *   - s_program 在首次初始化/加载时通过 iot_mem 分配，容量固定为
 *     MOTION_MAX_PROGRAM，之后重复初始化复用同一块内存。
 */

static char *s_program;                    /* 脚本文本副本（首次使用时分配） */
static motion_source_t s_source;           /* 主程序数据源 */
static motion_vm_t s_vm;                   /* 主程序 VM */
static bool s_loaded;                      /* 是否已成功加载脚本 */

/* 惰性分配脚本缓冲区；已分配则直接复用。 */
static int ensure_program(void)
{
    if (s_program == NULL) {
        s_program = (char *)iot_malloc(MOTION_MAX_PROGRAM);
        if (s_program == NULL) {
            return MOTION_ERR_MEMORY;
        }
    }
    return 0;
}

/*
 * 初始化解释器。
 * 返回：0 成功；MOTION_ERR_MEMORY 内存分配失败。
 * 注意：会重置设备、任务层、流程索引与主 VM，并清空脚本缓冲区。
 */
int motion_init(void)
{
    int rc = ensure_program();
    int task_rc;

    motion_device_reset();
    task_rc = motion_task_init();
    motion_flow_reset();
    motion_vm_init(&s_vm);
    motion_source_string_init(&s_source, s_program);
    if (s_program != NULL) {
        s_program[0] = '\0';
    }
    s_loaded = false;

    if (rc == 0) {
        rc = task_rc;
    }
    return rc;
}

/*
 * 加载脚本。
 * 参数：text 以 '\0' 结尾的脚本。
 * 返回：0 成功；MOTION_ERR_PARAMETER 参数为空；
 *       MOTION_ERR_MEMORY 超出静态缓冲区；其余为索引构建/JOB 注册错误。
 * 注意：失败时 s_loaded 置 false，主程序不可启动。
 */
int motion_load(const char *text)
{
    size_t length;
    int rc;
    uint8_t i;

    if (text == NULL) {
        return MOTION_ERR_PARAMETER;
    }

    rc = ensure_program();
    if (rc != 0) {
        return rc;
    }

    length = strlen(text);
    if (length >= (size_t)MOTION_MAX_PROGRAM) {
        return MOTION_ERR_MEMORY; /* 需为结尾 '\0' 预留 1 字节 */
    }

    memset(s_program, 0, MOTION_MAX_PROGRAM);
    memcpy(s_program, text, length);

    rc = motion_flow_build(s_program);
    if (rc != 0) {
        s_loaded = false;
        return rc;
    }

    rc = motion_task_init();
    if (rc != 0) {
        s_loaded = false;
        return rc;
    }
    /* 按索引逐条注册 JOB，任务层据此独立调度 */
    for (i = 0U; i < motion_flow_job_count(); i++) {
        const motion_job_entry_t *job = motion_flow_job_at(i);

        if (job == NULL) {
            continue;
        }
        rc = motion_task_add(job->name, job->segment, job->start_offset,
                             job->end_offset, s_program);
        if (rc != 0) {
            s_loaded = false;
            return rc;
        }
    }

    /* 主程序始终从偏移 0、段 0 开始，不限边界 */
    motion_source_string_init(&s_source, s_program);
    motion_vm_init(&s_vm);
    motion_vm_bind(&s_vm, &s_source, 0U, 0, false, 0U);

    s_loaded = true;
    return 0;
}

/* 启动主程序；未成功加载时拒绝启动 */
int motion_start(void)
{
    if (!s_loaded) {
        return MOTION_ERR_PARAMETER;
    }
    return motion_vm_start(&s_vm);
}

/* 暂停主程序 */
int motion_pause(void)
{
    return motion_vm_pause(&s_vm);
}

/* 恢复主程序 */
int motion_resume(void)
{
    return motion_vm_resume(&s_vm);
}

/* 停止主程序（置 COMPLETED） */
int motion_stop(void)
{
    return motion_vm_stop(&s_vm);
}

/* 中止主程序（置 ABORTED） */
int motion_abort(void)
{
    return motion_vm_abort(&s_vm);
}

/*
 * 周期调度入口：先推进主程序，再推进所有 JOB。
 * 注意：JOB 与主程序共享同一份脚本文本，但各自维护 pc 与流程栈。
 */
void motion_poll(void)
{
    motion_vm_poll(&s_vm);
    motion_task_poll();
}

/* 查询主程序状态（直接读取静态 VM 的 state） */
motion_state_t motion_state(void)
{
    return s_vm.state;
}

/* 返回主程序最近一次错误的只读指针（始终有效） */
const motion_error_t *motion_get_error(void)
{
    return &s_vm.error;
}
