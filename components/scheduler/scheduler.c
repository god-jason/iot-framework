/*
 * 定时任务调度实现（cron 表达式解析 + 分钟级匹配）。
 *
 * 解析阶段：把表达式 5 个字段分别展开成 64 位“时间掩码”，
 *           每一位代表该字段的一个取值（分 0~59、时 0~23、日 1~31、
 *           月 1~12、周 0~6，星期日统一映射到 bit0）。
 * 运行阶段：scheduler_process 将当前时间换算为分钟级时间戳，
 *           用掩码做按位与判断是否命中；同一分钟内只触发一次（last_minute 去重）。
 *
 * 日期与星期的组合语义遵循 cron 规则：
 *   两者都受限定时为“或”，任一为 * 时为“与”。
 *
 * 边界：任务表为固定容量（SCHEDULER_MAX_ENTRIES），运行期分配，添加失败返回 -1；
 *       表达式长度上限 SCHEDULER_EXPR_MAX（含结尾 '\0' 前的有效字符）。
 *       回调在调用 process 的上下文中同步执行。
 */
#include "scheduler.h"
#include "hal_mem.h"

#include <string.h>

/* 任务表容量（固定，不支持动态扩容） */
#define SCHEDULER_MAX_ENTRIES 8
/* cron 表达式最大长度（字符数，不含结尾 '\0'） */
#define SCHEDULER_EXPR_MAX    128

typedef struct {
    uint64_t minute_mask;   /* bit 0~59：允许的分钟 */
    uint64_t hour_mask;     /* bit 0~23：允许的小时 */
    uint64_t day_mask;      /* bit 1~31：允许的日 */
    uint64_t month_mask;    /* bit 1~12：允许的月 */
    uint64_t weekday_mask;  /* bit 0~6：允许的星期（0=周日） */

    bool day_star;          /* 日字段是否为 * */
    bool weekday_star;      /* 周字段是否为 * */
    bool reboot;            /* @reboot */

    scheduler_fn callback;  /* 触发回调 */
    void *user;             /* 回调上下文 */
    bool used;              /* 槽位是否已占用 */
    bool has_fired;         /* 是否已触发过（@reboot 用于只执行一次） */
    int64_t last_minute;    /* 上次触发的分钟时间戳，用于同分钟去重 */
} scheduler_entry_t;

/* 任务表指针，由 scheduler_init 或首次使用时分配 */
static scheduler_entry_t *s_entries;

/*
 * 惰性分配任务表（清零）。
 * 返回：0 成功（已分配或本次分配成功）；-1 分配失败。
 */
static int scheduler_ensure(void)
{
    if (s_entries != NULL) {
        return 0;
    }
    s_entries = (scheduler_entry_t *)hal_calloc(SCHEDULER_MAX_ENTRIES,
                                                sizeof(*s_entries));
    return (s_entries != NULL) ? 0 : -1;
}

/* 大小写不敏感的字符串比较，用于匹配 @yearly 等宏（要求长度完全相同） */
static bool sched_ieq(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0') {
        char ca = *a;
        char cb = *b;

        if (ca >= 'A' && ca <= 'Z') {
            ca = (char)(ca - 'A' + 'a');
        }
        if (cb >= 'A' && cb <= 'Z') {
            cb = (char)(cb - 'A' + 'a');
        }
        if (ca != cb) {
            return false;
        }
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

/*
 * 解析无符号十进制整数。
 * 参数：cursor 输入/输出游标（成功后前移到数字之后）；value 输出数值。
 * 返回：0 成功；-1 非数字开头或数值超过 100000（防御异常输入）。
 */
static int parse_uint(const char **cursor, uint32_t *value)
{
    const char *p = *cursor;
    uint32_t v = 0U;

    if (*p < '0' || *p > '9') {
        return -1;
    }
    while (*p >= '0' && *p <= '9') {
        v = v * 10U + (uint32_t)(*p - '0');
        if (v > 100000U) {
            return -1;
        }
        p++;
    }
    *cursor = p;
    *value = v;
    return 0;
}

/*
 * 解析逗号分隔列表中的一项：
 *   *、n、a-b、星号/n、a-b/n
 *
 * 参数：cursor 游标；min/max 字段取值上下界；is_weekday 是否为星期字段
 *       （为真时把 7 映射到 0）；mask 累加的位掩码；star 输出该项是否为 *。
 * 返回：0 成功；-1 语法错误或范围越界。
 * 说明：step 只在 * 或 a-b 之后允许出现；步进值为 0 视为非法。
 */
static int parse_item(const char **cursor, uint32_t min, uint32_t max,
                      bool is_weekday, uint64_t *mask, bool *star)
{
    const char *p = *cursor;
    uint32_t start = 0U;
    uint32_t end = 0U;
    uint32_t step = 1U;
    bool ranged = false;
    uint32_t v;

    if (*p == '*') {
        *star = true;
        ranged = true;
        start = min;
        end = max;
        p++;
    } else {
        if (parse_uint(&p, &start) != 0) {
            return -1;
        }
        if (*p == '-') {
            p++;
            ranged = true;
            if (parse_uint(&p, &end) != 0) {
                return -1;
            }
        } else {
            end = start;
        }
    }

    if (*p == '/') {
        p++;
        if (!ranged || parse_uint(&p, &step) != 0 || step == 0U) {
            return -1;
        }
    }

    if (start < min || start > max || end < min || end > max || start > end) {
        return -1;
    }

    for (v = start; v <= end; v += step) {
        /* 星期 7 与 0 等价，统一落到 bit0；其余直接按取值落位 */
        uint32_t bit = (is_weekday && v == 7U) ? 0U : v;

        *mask |= (1ULL << bit);
        /* 防止 v += step 越过上界后回绕（无符号） */
        if (end - v < step) {
            break;
        }
    }
    *cursor = p;
    return 0;
}

/*
 * 解析一个字段（支持逗号列表，逐项委托 parse_item）。
 * 参数：text 以 '\0' 结尾的字段文本；min/max 取值范围；is_weekday 星期标志；
 *       mask 输出掩码；star 输出是否包含 *。
 * 返回：0 成功；-1 语法或范围错误。
 * 说明：调用前 mask/star 会被清零；文本为空视为非法。
 */
static int parse_field(const char *text, uint32_t min, uint32_t max,
                       bool is_weekday, uint64_t *mask, bool *star)
{
    const char *p = text;

    *mask = 0U;
    *star = false;

    if (text == NULL || *text == '\0') {
        return -1;
    }

    for (;;) {
        if (parse_item(&p, min, max, is_weekday, mask, star) != 0) {
            return -1;
        }
        if (*p == ',') {
            p++;
            if (*p == '\0') {
                return -1;
            }
            continue;
        }
        return (*p == '\0') ? 0 : -1;
    }
}

/*
 * 按空白字符把字符串切分为若干字段（原地写入 '\0'）。
 * 参数：buf 可修改的表达式缓冲；fields 输出字段指针数组；max 最大字段数。
 * 返回：字段个数；-1 表示字段数超过 max。
 * 说明：连续空格/制表符会被跳过，不产生空字段。
 */
static int split_fields(char *buf, char **fields, int max)
{
    int count = 0;
    char *p = buf;

    while (*p != '\0') {
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (*p == '\0') {
            break;
        }
        if (count >= max) {
            return -1;
        }
        fields[count++] = p;
        while (*p != '\0' && *p != ' ' && *p != '\t') {
            p++;
        }
        if (*p != '\0') {
            *p++ = '\0';
        }
    }
    return count;
}

/*
 * 解析标准 5 字段 cron 表达式（buf 会被就地切分）。
 * 参数：buf 表达式文本；entry 输出掩码与 day_star/weekday_star。
 * 返回：0 成功；-1 字段数不为 5 或任一字段非法。
 * 说明：分/时/日/月/周的取值范围分别为 0-59、0-23、1-31、1-12、0-7。
 */
static int parse_fields(char *buf, scheduler_entry_t *entry)
{
    char *fields[5];
    bool any = false;
    int count = split_fields(buf, fields, 5);

    if (count != 5) {
        return -1;
    }
    if (parse_field(fields[0], 0U, 59U, false, &entry->minute_mask,
                    &any) != 0) {
        return -1;
    }
    if (parse_field(fields[1], 0U, 23U, false, &entry->hour_mask,
                    &any) != 0) {
        return -1;
    }
    if (parse_field(fields[2], 1U, 31U, false, &entry->day_mask,
                    &entry->day_star) != 0) {
        return -1;
    }
    if (parse_field(fields[3], 1U, 12U, false, &entry->month_mask,
                    &any) != 0) {
        return -1;
    }
    if (parse_field(fields[4], 0U, 7U, true, &entry->weekday_mask,
                    &entry->weekday_star) != 0) {
        return -1;
    }
    return 0;
}

/*
 * 解析 crontab 文本：先去除首尾空白，识别 @ 宏或按 5 字段解析。
 * 参数：expression 原始表达式；entry 输出的调度条目。
 * 返回：0 成功；-1 为空、超长、宏未知或字段语法错误。
 * 说明：@reboot 只置 reboot 标志，不做时间掩码解析；
 *       其余宏先展开为标准 5 字段文本，再复用 parse_fields。
 */
static int parse_crontab(const char *expression, scheduler_entry_t *entry)
{
    char buf[SCHEDULER_EXPR_MAX];
    const char *src = expression;
    const char *expanded = NULL;
    size_t len;

    if (expression == NULL) {
        return -1;
    }

    while (*src == ' ' || *src == '\t') {
        src++;
    }
    len = strlen(src);
    while (len > 0U && (src[len - 1U] == ' ' || src[len - 1U] == '\t')) {
        len--;
    }
    if (len == 0U || len >= sizeof(buf)) {
        return -1;
    }
    memcpy(buf, src, len);
    buf[len] = '\0';

    /* @ 开头为宏；@reboot 直接标记，其余展开为等价 5 字段表达式 */
    if (buf[0] == '@') {
        if (sched_ieq(buf, "@yearly") || sched_ieq(buf, "@annually")) {
            expanded = "0 0 1 1 *";
        } else if (sched_ieq(buf, "@monthly")) {
            expanded = "0 0 1 * *";
        } else if (sched_ieq(buf, "@weekly")) {
            expanded = "0 0 * * 0";
        } else if (sched_ieq(buf, "@daily") || sched_ieq(buf, "@midnight")) {
            expanded = "0 0 * * *";
        } else if (sched_ieq(buf, "@hourly")) {
            expanded = "0 * * * *";
        } else if (sched_ieq(buf, "@reboot")) {
            entry->reboot = true;
            return 0;
        } else {
            return -1;
        }
        if (strlen(expanded) >= sizeof(buf)) {
            return -1;
        }
        strcpy(buf, expanded);
    }

    return parse_fields(buf, entry);
}

/*
 * 判断当前时间是否满足条目的时间掩码。
 * 参数：entry 调度条目；now 当前时间（需已填好分/时/日/月/weekday）。
 * 返回：true 命中，false 不命中。
 * 说明：分、时、月必须全部命中；日与周按 cron 规则组合：
 *       两者均被限定（非 *）时为“或”，否则为“与”。
 *       传入的 weekday 先对 7 取模，兼容 0 与 7 都表示周日的约定。
 */
static bool sched_match(const scheduler_entry_t *entry,
                        const datetime_t *now)
{
    bool day_ok;
    bool weekday_ok;
    uint8_t weekday = (uint8_t)(now->weekday % 7U);

    if ((entry->minute_mask & (1ULL << now->minute)) == 0U ||
        (entry->hour_mask & (1ULL << now->hour)) == 0U ||
        (entry->month_mask & (1ULL << now->month)) == 0U) {
        return false;
    }

    day_ok = (entry->day_mask & (1ULL << now->day)) != 0U;
    weekday_ok = (entry->weekday_mask & (1ULL << weekday)) != 0U;

    if (entry->day_star || entry->weekday_star) {
        return day_ok && weekday_ok;
    }
    return day_ok || weekday_ok;
}

/* 初始化调度器：重建任务表并清空所有槽位及其运行状态。
 * 返回：0 成功；-1 内存分配失败。 */
int scheduler_init(void)
{
    int i;

    hal_free(s_entries);
    s_entries = NULL;
    if (scheduler_ensure() != 0) {
        return -1;
    }
    for (i = 0; i < SCHEDULER_MAX_ENTRIES; i++) {
        s_entries[i].used = false;
        s_entries[i].has_fired = false;
        s_entries[i].last_minute = 0;
    }
    return 0;
}

/*
 * 添加定时任务：寻找空闲槽位 -> 解析表达式 -> 填充条目。
 * 参数：crontab cron 表达式；callback 触发回调（非空）；user 回调上下文；
 *       id 输出任务标识（槽位下标，可为 NULL）。
 * 返回：0 成功；-1 参数非法、无空闲槽位或解析失败。
 * 说明：解析失败时不占用槽位（used 保持 false）。
 */
int scheduler_add(const char *crontab, scheduler_fn callback,
                  void *user, uint32_t *id)
{
    int i;
    scheduler_entry_t *entry;

    if (crontab == NULL || callback == NULL) {
        return -1;
    }
    if (scheduler_ensure() != 0) {
        return -1;
    }

    for (i = 0; i < SCHEDULER_MAX_ENTRIES; i++) {
        if (!s_entries[i].used) {
            break;
        }
    }
    if (i == SCHEDULER_MAX_ENTRIES) {
        return -1;
    }

    entry = &s_entries[i];
    memset(entry, 0, sizeof(*entry));
    if (parse_crontab(crontab, entry) != 0) {
        return -1;
    }

    entry->callback = callback;
    entry->user = user;
    entry->used = true;
    entry->has_fired = false;
    entry->last_minute = 0;

    if (id != NULL) {
        *id = (uint32_t)i;
    }
    return 0;
}

/*
 * 删除任务：仅清除 used 标志，槽位可被后续 add 复用。
 * 参数：id 任务标识（即槽位下标）。
 * 返回：0 成功；-1 下标越界或槽位未使用。
 */
int scheduler_remove(uint32_t id)
{
    if (s_entries == NULL || id >= SCHEDULER_MAX_ENTRIES ||
        !s_entries[id].used) {
        return -1;
    }
    s_entries[id].used = false;
    return 0;
}

/*
 * 调度主循环入口：检查所有任务并同步触发到期回调。
 * 参数：now 当前时间。
 * 返回：0 成功；-1 参数为空或时间转换失败。
 * 说明：时间戳按 60 秒整除得到分钟序号，用于同分钟去重；
 *       @reboot 任务只触发一次；回调在遍历过程中同步执行，
 *       若回调内增删任务需注意遍历语义（本实现直接读写静态表）。
 */
int scheduler_process(const datetime_t *now)
{
    int64_t timestamp;
    int64_t minute;
    int i;

    if (now == NULL) {
        return -1;
    }
    if (datetime_to_timestamp(now, &timestamp) != 0) {
        return -1;
    }

    minute = timestamp / 60;

    if (s_entries == NULL) {
        return 0;
    }

    /* 逐槽位检查：跳过空闲槽位，命中则更新状态并同步回调 */
    for (i = 0; i < SCHEDULER_MAX_ENTRIES; i++) {
        scheduler_entry_t *entry = &s_entries[i];

        if (!entry->used) {
            continue;
        }
        /* 去重：@reboot 只跑一次；普通任务同一分钟内只触发一次 */
        if (entry->reboot) {
            if (entry->has_fired) {
                continue;
            }
        } else if (entry->has_fired && entry->last_minute == minute) {
            continue;
        }

        /* @reboot 无条件执行，其余需通过时间掩码匹配 */
        if (!entry->reboot && !sched_match(entry, now)) {
            continue;
        }

        entry->has_fired = true;
        entry->last_minute = minute;
        if (entry->callback != NULL) {
            entry->callback(entry->user);
        }
    }
    return 0;
}
