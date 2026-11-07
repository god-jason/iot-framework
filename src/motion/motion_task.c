#include "motion_task.h"
#include "motion_device.h"
#include "motion_error.h"
#include "motion_parser.h"
#include "motion_source.h"
#include "iot_mem.h"

#include <string.h>

/*
 * JOB 任务层实现。
 *
 * 执行模型：
 *   - 加载阶段由 motion_load() 调用 motion_task_add()，把每个 JOB 段
 *     注册为独立任务，并扫描其代码范围收集会使用的 STEP/PWM 设备；
 *   - 启动时先做设备占用检查（acquire_resources），成功后再启动 VM；
 *   - 主循环调用 motion_task_poll()，逐个推进处于 RUNNING/WAITING 的 JOB，
 *     结束的任务自动释放设备（release_resources）。
 *
 * 数据流：
 *   每个 JOB 持有自己的 motion_source_t（指向共享脚本文本的字符串源）
 *   与 motion_vm_t；任务层不复制脚本文本，只记录偏移范围。
 *
 * 静态缓冲与边界：
 *   - JOB 表容量为 MOTION_MAX_JOBS；
 *   - 每个 JOB 最多登记 MOTION_MAX_JOB_DEVICES 个 STEP 与 PWM 设备；
 *   - 设备占用表按 MOTION_MAX_DEVICES 索引，值 0 表示空闲，
 *     否则存放“JOB 下标 + 1”；
 *   - JOB 表与设备占用表在首次 init 时通过 iot_mem 分配，重复 init 复用。
 */

/* 单个 JOB 任务的运行数据 */
typedef struct {
    bool used;              /* 表项是否已注册 */
    bool resources_held;    /* 是否已占用 STEP/PWM 设备 */
    char name[MOTION_MAX_JOB_NAME]; /* 规范化（大写）后的 JOB 名 */
    int segment;            /* JOB 段号 */
    uint32_t start_offset;  /* JOB 体首行偏移 */
    uint32_t end_offset;    /* JOB 结束偏移 */

    motion_source_t source; /* 该 JOB 私有的数据源 */
    motion_vm_t vm;         /* 该 JOB 私有的虚拟机 */

    uint16_t step_ids[MOTION_MAX_JOB_DEVICES]; /* JOB 用到的 STEP 设备 */
    uint8_t step_count;                        /* STEP 设备数量 */
    uint16_t pwm_ids[MOTION_MAX_JOB_DEVICES];  /* JOB 用到的 PWM 设备 */
    uint8_t pwm_count;                         /* PWM 设备数量 */
} motion_job_t;

static motion_job_t *s_jobs;                 /* JOB 表（惰性分配） */
static uint8_t s_job_count;                  /* 已注册 JOB 数量 */
static bool s_initialized;                   /* 是否已初始化 */

/* 0 表示空闲，其他值表示占用该设备的 JOB 下标 + 1 */
static int16_t *s_step_owner;                /* 步进设备占用表（惰性分配） */
static int16_t *s_pwm_owner;                 /* PWM 设备占用表（惰性分配） */

/* 惰性分配任务表与设备占用表；已分配则复用，全部成功返回 0。 */
static int ensure_tables(void)
{
    if (s_jobs == NULL) {
        s_jobs = (motion_job_t *)iot_calloc(MOTION_MAX_JOBS,
                                             sizeof(*s_jobs));
        if (s_jobs == NULL) {
            return MOTION_ERR_MEMORY;
        }
    }
    if (s_step_owner == NULL) {
        s_step_owner = (int16_t *)iot_calloc(MOTION_MAX_DEVICES,
                                              sizeof(*s_step_owner));
        if (s_step_owner == NULL) {
            return MOTION_ERR_MEMORY;
        }
    }
    if (s_pwm_owner == NULL) {
        s_pwm_owner = (int16_t *)iot_calloc(MOTION_MAX_DEVICES,
                                             sizeof(*s_pwm_owner));
        if (s_pwm_owner == NULL) {
            return MOTION_ERR_MEMORY;
        }
    }
    return 0;
}

/* 将名称规范化为大写并截断到 size-1 字符，保证以 '\0' 结尾 */
static void normalize_name(const char *name, char *out, size_t size)
{
    size_t i = 0U;

    if (size == 0U) {
        return;
    }

    while (name != NULL && name[i] != '\0' && (i + 1U) < size) {
        char c = name[i];

        if (c >= 'a' && c <= 'z') {
            c = (char)(c - 'a' + 'A');
        }
        out[i] = c;
        i++;
    }
    out[i] = '\0';
}

/*
 * 向设备列表追加一个设备 ID（去重）。
 * 表满或已存在时不做追加；本函数恒返回 true，调用方忽略返回值。
 */
static bool add_device_id(uint16_t *ids, uint8_t *count, uint16_t id)
{
    uint8_t i;

    if (*count >= MOTION_MAX_JOB_DEVICES) {
        return true;
    }

    for (i = 0U; i < *count; i++) {
        if (ids[i] == id) {
            return true;
        }
    }

    ids[*count] = id;
    (*count)++;
    return true;
}

/*
 * 扫描 JOB 代码范围 [start_offset, end_offset)，收集用到的 STEP/PWM 设备。
 * 参数：job 目标任务（读取其偏移范围，写回 step_ids/pwm_ids）；
 *       text 完整脚本文本。
 * 注意：只为资源占用检查服务，解析失败的物理行直接跳过。
 */
static void scan_devices(motion_job_t *job, const char *text)
{
    motion_source_t source;
    motion_instruction_t ins;
    uint32_t offset = job->start_offset;

    if (text == NULL || job->end_offset <= job->start_offset) {
        return;
    }

    motion_source_string_init(&source, text);
    (void)source.open(&source, NULL);

    while (offset < job->end_offset) {
        char buffer[MOTION_MAX_LINE];
        uint32_t next = offset;
        int rc;

        if (source.seek(&source, offset) != 0) {
            break;
        }
        rc = source.read_line(&source, buffer, (uint16_t)sizeof(buffer));
        if (rc <= 0) {
            break;
        }
        (void)source.tell(&source, &next);
        if (next <= offset) {
            break; /* 偏移未前进，防止死循环 */
        }

        if (motion_parser_parse(buffer, &ins) == MOTION_PARSE_OK) {
            if (ins.command == MOTION_CMD_STEP) {
                (void)add_device_id(job->step_ids, &job->step_count,
                                    ins.device_id);
            } else if (ins.command == MOTION_CMD_PWM) {
                (void)add_device_id(job->pwm_ids, &job->pwm_count,
                                    ins.device_id);
            }
        }

        offset = next;
    }

    (void)source.close(&source);
}

/*
 * 申请 JOB 所需设备。
 * 参数：index JOB 下标；job 目标任务。
 * 返回：0 成功；MOTION_ERR_RESOURCE_BUSY 有设备被其他 JOB 占用。
 * 注意：先整体检查再统一写入，避免出现部分占用；已由本 JOB 占用的
 *       设备允许重复申请。
 */
static int acquire_resources(uint8_t index, motion_job_t *job)
{
    uint8_t i;

    /* 第一阶段：只检查冲突，不修改占用表 */
    for (i = 0U; i < job->step_count; i++) {
        uint16_t id = job->step_ids[i];

        if (id < MOTION_MAX_DEVICES && s_step_owner[id] != 0 &&
            s_step_owner[id] != (int16_t)(index + 1U)) {
            return MOTION_ERR_RESOURCE_BUSY;
        }
    }

    for (i = 0U; i < job->pwm_count; i++) {
        uint16_t id = job->pwm_ids[i];

        if (id < MOTION_MAX_DEVICES && s_pwm_owner[id] != 0 &&
            s_pwm_owner[id] != (int16_t)(index + 1U)) {
            return MOTION_ERR_RESOURCE_BUSY;
        }
    }

    /* 第二阶段：检查通过后统一登记占用 */
    for (i = 0U; i < job->step_count; i++) {
        uint16_t id = job->step_ids[i];

        if (id < MOTION_MAX_DEVICES) {
            s_step_owner[id] = (int16_t)(index + 1U);
        }
    }
    for (i = 0U; i < job->pwm_count; i++) {
        uint16_t id = job->pwm_ids[i];

        if (id < MOTION_MAX_DEVICES) {
            s_pwm_owner[id] = (int16_t)(index + 1U);
        }
    }

    job->resources_held = true;
    return 0;
}

/* 释放 JOB 占用的设备；仅清除确实属于本 JOB 的占用项 */
static void release_resources(uint8_t index, motion_job_t *job)
{
    uint8_t i;

    if (!job->resources_held) {
        return;
    }

    for (i = 0U; i < job->step_count; i++) {
        uint16_t id = job->step_ids[i];

        if (id < MOTION_MAX_DEVICES &&
            s_step_owner[id] == (int16_t)(index + 1U)) {
            s_step_owner[id] = 0;
        }
    }
    for (i = 0U; i < job->pwm_count; i++) {
        uint16_t id = job->pwm_ids[i];

        if (id < MOTION_MAX_DEVICES &&
            s_pwm_owner[id] == (int16_t)(index + 1U)) {
            s_pwm_owner[id] = 0;
        }
    }

    job->resources_held = false;
}

/* 初始化任务层：清空 JOB 表与设备占用表。
 * 返回：0 成功；MOTION_ERR_MEMORY 内存分配失败。 */
int motion_task_init(void)
{
    if (ensure_tables() != 0) {
        s_job_count = 0U;
        s_initialized = false;
        return MOTION_ERR_MEMORY;
    }

    memset(s_jobs, 0, (size_t)MOTION_MAX_JOBS * sizeof(*s_jobs));
    memset(s_step_owner, 0, (size_t)MOTION_MAX_DEVICES *
                            sizeof(*s_step_owner));
    memset(s_pwm_owner, 0, (size_t)MOTION_MAX_DEVICES *
                           sizeof(*s_pwm_owner));
    s_job_count = 0U;
    s_initialized = true;
    return 0;
}

/*
 * 注册一个 JOB。
 * 参数：name JOB 名；segment 段号；start_offset/end_offset 代码范围；
 *       text 完整脚本文本（不复制，仅保存指针）。
 * 返回：0 成功；MOTION_ERR_UNSUPPORTED 未初始化；
 *       MOTION_ERR_PARAMETER 名称为空或重名；MOTION_ERR_MEMORY 表已满。
 */
int motion_task_add(const char *name, int segment, uint32_t start_offset,
                    uint32_t end_offset, const char *text)
{
    motion_job_t *job;
    char normalized[MOTION_MAX_JOB_NAME];
    uint8_t i;

    if (!s_initialized) {
        return MOTION_ERR_UNSUPPORTED;
    }
    if (s_jobs == NULL) {
        return MOTION_ERR_MEMORY;
    }
    if (name == NULL || name[0] == '\0') {
        return MOTION_ERR_PARAMETER;
    }

    /* 名称统一转大写后查重，保证大小写不敏感 */
    normalize_name(name, normalized, sizeof(normalized));
    for (i = 0U; i < s_job_count; i++) {
        if (s_jobs[i].used &&
            strcmp(s_jobs[i].name, normalized) == 0) {
            return MOTION_ERR_PARAMETER;
        }
    }

    if (s_job_count >= MOTION_MAX_JOBS) {
        return MOTION_ERR_MEMORY;
    }

    job = &s_jobs[s_job_count];
    memset(job, 0, sizeof(*job));
    job->used = true;
    strncpy(job->name, normalized, sizeof(job->name) - 1U);
    job->segment = segment;
    job->start_offset = start_offset;
    job->end_offset = end_offset;

    motion_source_string_init(&job->source, text);
    motion_vm_init(&job->vm);
    motion_vm_bind(&job->vm, &job->source, start_offset, segment, true,
                   end_offset);

    scan_devices(job, text);

    s_job_count++;
    return 0;
}

/*
 * 按名称查找 JOB 下标。
 * 返回：>=0 下标；未初始化、参数为空或未找到返回 -1。
 */
int motion_task_find(const char *name)
{
    char normalized[MOTION_MAX_JOB_NAME];
    uint8_t i;

    if (!s_initialized || s_jobs == NULL || name == NULL) {
        return -1;
    }

    normalize_name(name, normalized, sizeof(normalized));
    for (i = 0U; i < s_job_count; i++) {
        if (s_jobs[i].used &&
            strcmp(s_jobs[i].name, normalized) == 0) {
            return (int)i;
        }
    }
    return -1;
}

/*
 * 启动指定 JOB。
 * 返回：0 成功（已在运行时也返回 0）；MOTION_ERR_UNSUPPORTED 未初始化；
 *       MOTION_ERR_PARAMETER 未找到；MOTION_ERR_RESOURCE_BUSY 设备冲突；
 *       其余为 VM 启动错误（此时已回滚设备占用）。
 */
int motion_task_start(const char *name)
{
    int index;
    motion_job_t *job;
    int rc;

    if (!s_initialized) {
        return MOTION_ERR_UNSUPPORTED;
    }

    index = motion_task_find(name);
    if (index < 0) {
        return MOTION_ERR_PARAMETER;
    }

    job = &s_jobs[index];

    if (job->vm.state == MOTION_STATE_RUNNING ||
        job->vm.state == MOTION_STATE_WAITING ||
        job->vm.state == MOTION_STATE_PAUSED) {
        return 0; /* 已在运行或暂停中，重复启动视为成功 */
    }

    rc = acquire_resources((uint8_t)index, job);
    if (rc != 0) {
        return rc;
    }

    rc = motion_vm_start(&job->vm);
    if (rc != 0) {
        release_resources((uint8_t)index, job); /* 启动失败需归还设备 */
        return rc;
    }

    return 0;
}

/*
 * 停止指定 JOB：停止 VM 并释放其设备占用。
 * 返回：0 成功；MOTION_ERR_UNSUPPORTED 未初始化；MOTION_ERR_PARAMETER 未找到。
 */
int motion_task_stop(const char *name)
{
    int index;
    motion_job_t *job;

    if (!s_initialized) {
        return MOTION_ERR_UNSUPPORTED;
    }

    index = motion_task_find(name);
    if (index < 0) {
        return MOTION_ERR_PARAMETER;
    }

    job = &s_jobs[index];
    (void)motion_vm_stop(&job->vm);
    release_resources((uint8_t)index, job);
    return 0;
}

/*
 * 调度所有 JOB 一次：仅推进 RUNNING/WAITING 的任务；
 * 处于结束状态的 JOB 每轮都会检查并释放设备（可重复调用）。
 */
void motion_task_poll(void)
{
    uint8_t i;

    if (!s_initialized || s_jobs == NULL) {
        return;
    }

    for (i = 0U; i < s_job_count; i++) {
        motion_job_t *job = &s_jobs[i];

        if (!job->used) {
            continue;
        }

        if (job->vm.state == MOTION_STATE_RUNNING ||
            job->vm.state == MOTION_STATE_WAITING) {
            motion_vm_poll(&job->vm);
        }

        /* JOB 结束（完成/错误/中止/空闲）后归还设备，供其他 JOB 使用 */
        if (job->vm.state == MOTION_STATE_COMPLETED ||
            job->vm.state == MOTION_STATE_ERROR ||
            job->vm.state == MOTION_STATE_ABORTED ||
            job->vm.state == MOTION_STATE_IDLE) {
            release_resources(i, job);
        }
    }
}

/* 查询 JOB 状态；未初始化或未找到时返回 MOTION_STATE_IDLE */
motion_state_t motion_task_state(const char *name)
{
    int index;

    if (!s_initialized) {
        return MOTION_STATE_IDLE;
    }

    index = motion_task_find(name);
    if (index < 0) {
        return MOTION_STATE_IDLE;
    }

    return s_jobs[index].vm.state;
}

/* 返回已注册的 JOB 数量（未初始化时为 0） */
uint8_t motion_task_count(void)
{
    return s_job_count;
}
