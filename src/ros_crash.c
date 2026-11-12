/*
 * ros_crash.c - 崩溃现场记录
 *
 * 持久化：记录放在 .noinit 段（链接脚本 NOLOAD），复位不清零；
 *         上电后先用魔数 + 版本 + CRC 校验，避免把随机 RAM 当成记录。
 * 采集  ：平台层（Cortex-M 故障处理 / PC 信号处理）填好 ros_crash_context_t，
 *         本模块补全任务、tick、编译时间、栈快照并计算 CRC。
 * 上报  ：ros_crash_format() 生成文本，应用可通过 4G/MQTT/串口发送；
 *         上报成功后 ros_crash_clear()。
 *
 * 边界：
 *   - 同一次启动内只保留第一次现场（第一次异常才是根因）；
 *   - 所有写操作不做动态内存分配，可在异常上下文调用；
 *   - 断电仍会丢失（RAM 不保持），掉电保存需应用自行写入 Flash。
 */

#include "ros_crash.h"

#if ROS_CRASH_ENABLE

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "ros.h"
#include "ros_port.h"

/*
 * 记录存储：优先使用平台提供的复位保持区（如 STM32 的 .noinit），
 * 平台不支持时退回普通静态区（复位/断电后丢失）。
 */
static ros_crash_record_t s_record_fallback;
static ros_crash_record_t *s_rec = &s_record_fallback;

static bool s_valid;              /* 记录有效（魔数/版本/CRC 通过） */
static bool s_recorded_this_boot; /* 本次启动是否已记录（只保留第一次） */

/* ------------------------------ 内部工具 ------------------------------ */

/* 逐位 CRC32（多项式 0xEDB88320），无需查表，适合小体积固件 */
static uint32_t ros_crc32(const void *data, uint32_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFFUL;
    uint32_t i;
    int bit;

    for (i = 0U; i < len; i++) {
        crc ^= p[i];
        for (bit = 0; bit < 8; bit++) {
            if ((crc & 1U) != 0U) {
                crc = (crc >> 1) ^ 0xEDB88320UL;
            } else {
                crc >>= 1;
            }
        }
    }
    return ~crc;
}

/* 记录除 crc 字段外全部字节的校验值 */
static uint32_t ros_crash_crc(const ros_crash_record_t *rec)
{
    return ros_crc32(rec, (uint32_t)offsetof(ros_crash_record_t, crc));
}

/* 带截断的字符串拷贝，保证 '\0' 结尾；src 为 NULL 时写入空串 */
static void ros_copy_str(char *dst, uint32_t size, const char *src)
{
    uint32_t n;

    if (size == 0U) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    n = (uint32_t)strlen(src);
    if (n >= size) {
        n = size - 1U;
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

/*
 * 采集崩溃点附近的栈快照。
 * 仅当 SP 落在当前任务栈范围内才读取：异常可能发生在中断或栈已损坏的
 * 场景，盲目解引用非法 SP 会触发二次异常，导致连记录都写不下去。
 */
static void ros_crash_snapshot(const ros_crash_context_t *ctx)
{
    const ros_task_t *task = ros_current_task;
    uintptr_t base;
    uintptr_t end;
    uint32_t sp;
    uint32_t avail;
    uint32_t i;

    if (ctx == NULL || task == NULL || task->stack_mem == NULL ||
        task->stack_size == 0U || ctx->sp == 0U) {
        return;
    }

    base = (uintptr_t)task->stack_mem;
    end = base + task->stack_size;
    sp = ctx->sp;

    if ((uintptr_t)sp < base || (uintptr_t)sp >= end) {
        return;
    }
    avail = (uint32_t)((end - (uintptr_t)sp) / sizeof(uint32_t));
    if (avail > ROS_CRASH_STACK_WORDS) {
        avail = ROS_CRASH_STACK_WORDS;
    }
    if (avail == 0U) {
        return;
    }

    s_rec->stack_sp = sp;
    for (i = 0U; i < avail; i++) {
        s_rec->stack[i] = ((const uint32_t *)(uintptr_t)sp)[i];
    }
    s_rec->stack_words = (uint16_t)avail;
}

/* ------------------------------ 对外接口 ------------------------------ */

/*
 * 初始化崩溃记录：安装平台异常钩子，并校验平台复位保持区中上次留下的记录。
 * 有效记录会立即打印一份完整报告。
 */
void ros_crash_init(void)
{
    void *buf;

    s_recorded_this_boot = false;
    s_valid = false;

    /* 选择持久化存储：平台提供复位保持区时优先使用 */
    buf = ros_port_crash_buffer((uint32_t)sizeof(ros_crash_record_t));
    s_rec = (buf != NULL) ? (ros_crash_record_t *)buf : &s_record_fallback;

    /* 安装异常捕获（PC 信号 / Cortex-M 故障处理） */
    ros_crash_port_install();

    if (s_rec->magic == ROS_CRASH_MAGIC &&
        s_rec->version == ROS_CRASH_VERSION &&
        s_rec->reason > (uint16_t)ROS_CRASH_NONE &&
        s_rec->reason <= (uint16_t)ROS_CRASH_USER &&
        s_rec->crc == ros_crash_crc(s_rec)) {
        s_valid = true;
        /* 上次启动的现场：主动打印一份，串口/调试器第一时间可见 */
        ros_crash_print(s_rec);
    } else {
        memset(s_rec, 0, sizeof(*s_rec));
    }
}

/* 是否存在上次启动遗留的有效崩溃记录（1 = 有）。 */
int ros_crash_pending(void)
{
    return s_valid ? 1 : 0;
}

/* 获取有效崩溃记录；无记录返回 NULL。 */
const ros_crash_record_t *ros_crash_get(void)
{
    return s_valid ? s_rec : NULL;
}

/* 清除崩溃记录，并允许本次启动重新记录。 */
void ros_crash_clear(void)
{
    memset(s_rec, 0, sizeof(*s_rec));
    s_valid = false;
    s_recorded_this_boot = false;
}

/*
 * 写入一次崩溃现场（可在中断/异常上下文调用）。
 * 补全任务、tick、编译时间与栈快照后计算 CRC；同一次启动只保留第一次。
 */
int ros_crash_record(int reason, const ros_crash_context_t *ctx)
{
    ros_task_t *task;
    uint32_t seq = 1U;

    if (reason <= ROS_CRASH_NONE || reason > ROS_CRASH_USER) {
        return -1;
    }

    ROS_TRACE(ROS_EVT_CRASH, (uint32_t)reason, 0U);
    ros_trace_freeze();   /* 保留崩溃前最后一批事件，供复位前/调试器读取 */

    if (s_recorded_this_boot) {
        return 0;   /* 保留第一次现场，后续异常多为连锁反应 */
    }
    if (s_valid) {
        seq = s_rec->seq + 1U;   /* 跨复位累加崩溃序号 */
    }

    memset(s_rec, 0, sizeof(*s_rec));
    s_rec->magic = ROS_CRASH_MAGIC;
    s_rec->version = ROS_CRASH_VERSION;
    s_rec->reason = (uint16_t)reason;
    s_rec->seq = seq;
    s_rec->tick = ros_tick_get();
    ros_copy_str(s_rec->build, sizeof(s_rec->build), __DATE__ " " __TIME__);

    task = ros_current_task;
    if (task != NULL) {
        ros_copy_str(s_rec->task, sizeof(s_rec->task), task->name);
        s_rec->task_priority = (uint16_t)task->priority;
        s_rec->task_state = (uint16_t)task->state;
#if ROS_USE_STACK_CHECK
        {
            ros_task_stats_t st;

            if (ros_task_get_stats(task, &st) == ROS_OK) {
                s_rec->task_stack_free = st.stack_free_bytes;
            }
        }
#endif
    }

    if (ctx != NULL) {
        s_rec->sp = ctx->sp;
        memcpy(s_rec->regs, ctx->regs, sizeof(s_rec->regs));
        s_rec->cfsr = ctx->cfsr;
        s_rec->hfsr = ctx->hfsr;
        s_rec->mmfar = ctx->mmfar;
        s_rec->bfar = ctx->bfar;
        s_rec->afsr = ctx->afsr;
        s_rec->exc_return = ctx->exc_return;
        s_rec->code = ctx->code;
        s_rec->line = ctx->line;
        ros_copy_str(s_rec->file, sizeof(s_rec->file), ctx->file);
        ros_copy_str(s_rec->expr, sizeof(s_rec->expr), ctx->expr);
        ros_crash_snapshot(ctx);
    }

    s_rec->crc = ros_crash_crc(s_rec);
    s_valid = true;
    s_recorded_this_boot = true;
    return 0;
}

/* 崩溃原因枚举转短名称（未知原因返回 "UNKNOWN"）。 */
const char *ros_crash_reason_name(int reason)
{
    switch (reason) {
    case ROS_CRASH_NONE:          return "NONE";
    case ROS_CRASH_ASSERT:        return "ASSERT";
    case ROS_CRASH_HARD_FAULT:    return "HARD_FAULT";
    case ROS_CRASH_MEMMANAGE:     return "MEMMANAGE";
    case ROS_CRASH_BUSFAULT:      return "BUSFAULT";
    case ROS_CRASH_USAGEFAULT:    return "USAGEFAULT";
    case ROS_CRASH_STACK_OVERFLOW:return "STACK_OVERFLOW";
    case ROS_CRASH_SIGNAL:        return "SIGNAL";
    case ROS_CRASH_WATCHDOG:      return "WATCHDOG";
    case ROS_CRASH_USER:          return "USER";
    default:                      return "UNKNOWN";
    }
}

/* 追加格式化输出，返回新的写入位置（已截断时返回 max-1） */
static int ros_crash_append(char *out, uint32_t max, int pos,
                            const char *fmt, ...)
{
    va_list ap;
    int n;

    if (out == NULL || max == 0U || pos < 0) {
        return -1;
    }
    if ((uint32_t)pos >= max) {
        return (int)(max - 1U);
    }

    va_start(ap, fmt);
    n = vsnprintf(out + pos, max - (uint32_t)pos, fmt, ap);
    va_end(ap);

    if (n < 0) {
        return pos;
    }
    if ((uint32_t)n >= max - (uint32_t)pos) {
        return (int)(max - 1U);
    }
    return pos + n;
}

/*
 * 把记录格式化为可上报的多行文本（行尾 "\r\n"）。
 * 返回写入字符数；空间不足时截断并保证 '\0' 结尾。
 */
int ros_crash_format(const ros_crash_record_t *rec, char *out, uint32_t max)
{
    int pos = 0;
    uint32_t i;

    if (rec == NULL || out == NULL || max == 0U) {
        return -1;
    }
    out[0] = '\0';

    pos = ros_crash_append(out, max, pos,
        "CRASH v%u seq=%lu reason=%s tick=%lu build=%s\r\n",
        (unsigned)rec->version, (unsigned long)rec->seq,
        ros_crash_reason_name((int)rec->reason),
        (unsigned long)rec->tick, rec->build);
    pos = ros_crash_append(out, max, pos,
        "task=%s prio=%u state=%u stack_free=%lu\r\n",
        (rec->task[0] != '\0') ? rec->task : "-",
        (unsigned)rec->task_priority, (unsigned)rec->task_state,
        (unsigned long)rec->task_stack_free);
    pos = ros_crash_append(out, max, pos,
        "pc=%08lX lr=%08lX sp=%08lX xpsr=%08lX\r\n",
        (unsigned long)rec->regs[6], (unsigned long)rec->regs[5],
        (unsigned long)rec->sp, (unsigned long)rec->regs[7]);
    pos = ros_crash_append(out, max, pos,
        "r0=%08lX r1=%08lX r2=%08lX r3=%08lX r12=%08lX\r\n",
        (unsigned long)rec->regs[0], (unsigned long)rec->regs[1],
        (unsigned long)rec->regs[2], (unsigned long)rec->regs[3],
        (unsigned long)rec->regs[4]);
    pos = ros_crash_append(out, max, pos,
        "cfsr=%08lX hfsr=%08lX mmfar=%08lX bfar=%08lX afsr=%08lX exc=%08lX\r\n",
        (unsigned long)rec->cfsr, (unsigned long)rec->hfsr,
        (unsigned long)rec->mmfar, (unsigned long)rec->bfar,
        (unsigned long)rec->afsr, (unsigned long)rec->exc_return);
    pos = ros_crash_append(out, max, pos,
        "code=%08lX file=%s:%lu expr=%s\r\n",
        (unsigned long)rec->code,
        (rec->file[0] != '\0') ? rec->file : "-",
        (unsigned long)rec->line,
        (rec->expr[0] != '\0') ? rec->expr : "-");

    for (i = 0U; i < rec->stack_words; i += 4U) {
        uint32_t j;

        pos = ros_crash_append(out, max, pos, "stack[%08lX]:",
                               (unsigned long)(rec->stack_sp + i * 4U));
        for (j = 0U; j < 4U && (i + j) < rec->stack_words; j++) {
            pos = ros_crash_append(out, max, pos, " %08lX",
                                   (unsigned long)rec->stack[i + j]);
        }
        pos = ros_crash_append(out, max, pos, "\r\n");
    }

    return (pos < 0) ? 0 : pos;
}

/* 把崩溃现场打印到日志串口/stdout（不依赖日志等级，始终输出）。 */
void ros_crash_print(const ros_crash_record_t *rec)
{
    uint32_t i;

    if (rec == NULL) {
        return;
    }

    printf("[F][crash] v%u seq=%lu reason=%s tick=%lu\r\n",
           (unsigned)rec->version, (unsigned long)rec->seq,
           ros_crash_reason_name((int)rec->reason),
           (unsigned long)rec->tick);
    printf("[F][crash] build=%s task=%s prio=%u state=%u stack_free=%lu\r\n",
           rec->build, (rec->task[0] != '\0') ? rec->task : "-",
           (unsigned)rec->task_priority, (unsigned)rec->task_state,
           (unsigned long)rec->task_stack_free);
    printf("[F][crash] pc=%08lX lr=%08lX sp=%08lX xpsr=%08lX\r\n",
           (unsigned long)rec->regs[6], (unsigned long)rec->regs[5],
           (unsigned long)rec->sp, (unsigned long)rec->regs[7]);
    printf("[F][crash] r0=%08lX r1=%08lX r2=%08lX r3=%08lX r12=%08lX\r\n",
           (unsigned long)rec->regs[0], (unsigned long)rec->regs[1],
           (unsigned long)rec->regs[2], (unsigned long)rec->regs[3],
           (unsigned long)rec->regs[4]);
    printf("[F][crash] cfsr=%08lX hfsr=%08lX mmfar=%08lX bfar=%08lX afsr=%08lX exc=%08lX\r\n",
           (unsigned long)rec->cfsr, (unsigned long)rec->hfsr,
           (unsigned long)rec->mmfar, (unsigned long)rec->bfar,
           (unsigned long)rec->afsr, (unsigned long)rec->exc_return);
    printf("[F][crash] code=%08lX file=%s:%lu expr=%s\r\n",
           (unsigned long)rec->code,
           (rec->file[0] != '\0') ? rec->file : "-",
           (unsigned long)rec->line,
           (rec->expr[0] != '\0') ? rec->expr : "-");

    for (i = 0U; i < rec->stack_words; i += 4U) {
        uint32_t j;

        printf("[F][crash] stack %08lX:",
               (unsigned long)(rec->stack_sp + i * 4U));
        for (j = 0U; j < 4U && (i + j) < rec->stack_words; j++) {
            printf(" %08lX", (unsigned long)rec->stack[i + j]);
        }
        printf("\r\n");
    }
}

#else /* ROS_CRASH_ENABLE */

/* 崩溃记录被裁剪时的空实现：保持接口可链接，不产生任何存储与代码开销。 */

void ros_crash_init(void)
{
}

int ros_crash_pending(void)
{
    return 0;
}

const ros_crash_record_t *ros_crash_get(void)
{
    return NULL;
}

void ros_crash_clear(void)
{
}

int ros_crash_record(int reason, const ros_crash_context_t *ctx)
{
    (void)reason;
    (void)ctx;
    return -1;
}

int ros_crash_format(const ros_crash_record_t *rec, char *out, uint32_t max)
{
    (void)rec;
    (void)out;
    (void)max;
    return -1;
}

void ros_crash_print(const ros_crash_record_t *rec)
{
    (void)rec;
}

const char *ros_crash_reason_name(int reason)
{
    (void)reason;
    return "UNKNOWN";
}

#endif /* ROS_CRASH_ENABLE */
