#include "motion_flow.h"
#include "motion_error.h"
#include "motion_parser.h"
#include "motion_source.h"
#include "iot_mem.h"

#include <string.h>

/*
 * 流程索引与表达式求值模块实现。
 *
 * 执行模型：
 *   - motion_flow_build() 在加载阶段逐行读取源码，用显式的配对栈
 *     （open_if / open_loop）把 IF/LOOP 的起始与结束绑定，结果写入下面的
 *     静态表；构建完成后这些表视为只读。
 *   - 运行期只通过 motion_flow_find_* / motion_flow_job_at 等接口按偏移
 *     或名称线性查找（表规模受 MOTION_MAX_* 限制，查找开销可接受）。
 *   - motion_flow_eval() 是独立的递归下降求值器，按
 *     OR < AND < 比较 < NOT < 加减 < 乘除模 < 一元 < 原子 的优先级组织。
 *
 * 数据流/依赖：
 *   motion_source 提供文本，motion_parser 逐行解析为 motion_instruction_t；
 *   本模块只使用命令种类、标签名与偏移。表达式求值阶段通过
 *   motion_variable_table_t 解析 $变量。
 *
 * 静态缓冲与边界：
 *   - 索引表容量由 MOTION_MAX_FLOW_BLOCKS / MOTION_MAX_LABELS /
 *     MOTION_MAX_JOBS / MOTION_MAX_LINES 限定，越界返回 MOTION_ERR_MEMORY。
 *   - 各索引表在首次 reset/build 时通过 iot_mem 分配，之后复用。
 *   - 嵌套配对栈深度受 MOTION_MAX_FLOW_DEPTH 限制。
 *   - 异常处理表大小为 MOTION_MAX_JOBS + 1：下标 0 为主程序段，
 *     1..MOTION_MAX_JOBS 对应各 JOB 段。
 *   - 行偏移表只记录前 MOTION_MAX_LINES 行。
 */

static motion_if_entry_t *s_ifs;        /* IF 块索引表（惰性分配） */
static uint16_t s_if_count;             /* 已登记 IF 数量 */

static motion_loop_entry_t *s_loops;    /* LOOP 块索引表（惰性分配） */
static uint16_t s_loop_count;           /* 已登记 LOOP 数量 */

static motion_label_t *s_labels;        /* 标签表（惰性分配） */
static uint16_t s_label_count;          /* 已登记标签数量 */

static motion_job_entry_t *s_jobs;      /* JOB 表（惰性分配） */
static uint16_t s_job_count;            /* 已登记 JOB 数量 */

/* 异常处理表：下标 0 为主程序段，1..MOTION_MAX_JOBS 对应各 JOB 段 */
static motion_error_entry_t *s_errors;  /* 惰性分配 */

static uint32_t *s_line_offsets;        /* 行首偏移表，下标即行号-1（惰性分配） */
static uint16_t s_line_count;           /* 已记录行数 */

/* 惰性分配全部索引表；已分配则复用，全部成功返回 0。 */
static int ensure_tables(void)
{
    if (s_ifs == NULL) {
        s_ifs = (motion_if_entry_t *)iot_calloc(MOTION_MAX_FLOW_BLOCKS,
                                                 sizeof(*s_ifs));
        if (s_ifs == NULL) {
            return MOTION_ERR_MEMORY;
        }
    }
    if (s_loops == NULL) {
        s_loops = (motion_loop_entry_t *)iot_calloc(MOTION_MAX_FLOW_BLOCKS,
                                                     sizeof(*s_loops));
        if (s_loops == NULL) {
            return MOTION_ERR_MEMORY;
        }
    }
    if (s_labels == NULL) {
        s_labels = (motion_label_t *)iot_calloc(MOTION_MAX_LABELS,
                                                 sizeof(*s_labels));
        if (s_labels == NULL) {
            return MOTION_ERR_MEMORY;
        }
    }
    if (s_jobs == NULL) {
        s_jobs = (motion_job_entry_t *)iot_calloc(MOTION_MAX_JOBS,
                                                   sizeof(*s_jobs));
        if (s_jobs == NULL) {
            return MOTION_ERR_MEMORY;
        }
    }
    if (s_errors == NULL) {
        s_errors = (motion_error_entry_t *)iot_calloc(
            MOTION_MAX_JOBS + 1U, sizeof(*s_errors));
        if (s_errors == NULL) {
            return MOTION_ERR_MEMORY;
        }
    }
    if (s_line_offsets == NULL) {
        s_line_offsets = (uint32_t *)iot_calloc(MOTION_MAX_LINES,
                                                 sizeof(*s_line_offsets));
        if (s_line_offsets == NULL) {
            return MOTION_ERR_MEMORY;
        }
    }
    return 0;
}

/* 清空所有索引表及计数，为下一次构建做准备 */
void motion_flow_reset(void)
{
    s_if_count = 0U;
    s_loop_count = 0U;
    s_label_count = 0U;
    s_job_count = 0U;
    s_line_count = 0U;

    if (ensure_tables() != 0) {
        return; /* 分配失败：计数已清零，后续 build 会再次尝试并上报 */
    }

    memset(s_ifs, 0, (size_t)MOTION_MAX_FLOW_BLOCKS * sizeof(*s_ifs));
    memset(s_loops, 0, (size_t)MOTION_MAX_FLOW_BLOCKS * sizeof(*s_loops));
    memset(s_labels, 0, (size_t)MOTION_MAX_LABELS * sizeof(*s_labels));
    memset(s_jobs, 0, (size_t)MOTION_MAX_JOBS * sizeof(*s_jobs));
    memset(s_errors, 0, (size_t)(MOTION_MAX_JOBS + 1U) * sizeof(*s_errors));
    memset(s_line_offsets, 0, (size_t)MOTION_MAX_LINES *
                             sizeof(*s_line_offsets));
}

/*
 * 登记一个标签。
 * 参数：
 *   ins         - 已解析的 LABEL 指令（取其中 name）；
 *   offset      - 标签行字节偏移；
 *   next_offset - 标签下一行偏移（实际跳转落点）；
 *   line        - 源码行号（从 1 开始）；
 *   segment     - 所属段（0 主程序，>0 JOB 段）。
 * 返回：0 成功；MOTION_ERR_FLOW 名称为空或同段重名；
 *       MOTION_ERR_MEMORY 标签表已满。
 * 注意：标签按段隔离，不同段允许同名。
 */
static int add_label(const motion_instruction_t *ins, uint32_t offset,
                     uint32_t next_offset, uint32_t line, int segment)
{
    uint16_t i;

    if (ins->name[0] == '\0') {
        return MOTION_ERR_FLOW;
    }

    for (i = 0U; i < s_label_count; i++) {
        if (s_labels[i].segment == segment &&
            strcmp(s_labels[i].name, ins->name) == 0) {
            return MOTION_ERR_FLOW;
        }
    }

    if (s_label_count >= MOTION_MAX_LABELS) {
        return MOTION_ERR_MEMORY;
    }

    memset(&s_labels[s_label_count], 0, sizeof(s_labels[s_label_count]));
    strncpy(s_labels[s_label_count].name, ins->name,
            sizeof(s_labels[s_label_count].name) - 1U);
    s_labels[s_label_count].offset = offset;
    s_labels[s_label_count].next_offset = next_offset;
    s_labels[s_label_count].line = line;
    s_labels[s_label_count].segment = segment;
    s_label_count++;
    return 0;
}

/*
 * 扫描整段脚本并建立流程索引。
 * 参数：text 以 '\0' 结尾的脚本；NULL 视为空脚本并直接返回 0。
 * 返回：0 成功；MOTION_ERR_FLOW 结构不配对（缺 END/ENDLOOP/ENDJOB/
 *       ENDERROR，或出现无归属的 ELSE/END 等）；MOTION_ERR_MEMORY
 *       索引表或配对栈溢出。
 * 注意事项：
 *   - 进入时先清空全部静态表；构建失败不会自动回滚，需重新加载。
 *   - open_if/open_loop 保存未闭合块在对应表中的下标，按后进先出配对。
 *   - 主程序段号为 0，JOB 段号为 JOB 登记序号 + 1，JOB 内不允许嵌套 JOB。
 *   - 若源码在块中间结束，由函数末尾的统一校验报错。
 */
int motion_flow_build(const char *text)
{
    motion_source_t source;
    motion_instruction_t ins;
    uint16_t open_if[MOTION_MAX_FLOW_DEPTH];   /* 未闭合 IF 的索引栈 */
    uint16_t open_loop[MOTION_MAX_FLOW_DEPTH]; /* 未闭合 LOOP 的索引栈 */
    uint16_t if_depth = 0U;                    /* 当前 IF 嵌套深度 */
    uint16_t loop_depth = 0U;                  /* 当前 LOOP 嵌套深度 */
    uint32_t offset = 0U;                      /* 当前行起始偏移 */
    uint32_t line = 1U;                        /* 当前行号 */
    int segment = 0;                           /* 当前段号 */
    int job_index = -1;                        /* 当前打开的 JOB 下标，-1 表示不在 JOB 内 */
    int error_segment = -1;                    /* 当前打开的 ON ERROR 所属段 */
    int result = 0;

    motion_flow_reset();

    if (ensure_tables() != 0) {
        return MOTION_ERR_MEMORY;
    }

    if (text == NULL) {
        return 0;
    }

    motion_source_string_init(&source, text);
    if (source.open == NULL || source.open(&source, NULL) != 0) {
        return MOTION_ERR_FLOW;
    }

    for (;;) {
        char buffer[MOTION_MAX_LINE];
        uint32_t next_offset = offset;
        motion_parse_result_t parsed;
        int read_result = source.read_line(&source, buffer,
                                           (uint16_t)sizeof(buffer));

        if (read_result == 0) {
            break; /* 读到文件末尾 */
        }
        if (read_result < 0) {
            result = MOTION_ERR_FLOW; /* 读错误或行超长 */
            break;
        }

        (void)source.tell(&source, &next_offset); /* 记录下一行起始偏移 */

        if (line <= MOTION_MAX_LINES) {
            /* 行号从 1 开始；超出容量后不再记录，line_at 对后续行返回 0 */
            s_line_offsets[s_line_count++] = offset;
        }

        parsed = motion_parser_parse(buffer, &ins);
        if (parsed == MOTION_PARSE_OK) {
            switch (ins.command) {
            case MOTION_CMD_LABEL:
                /* 标签在加载期登记，GOTO/CALL 运行时直接查表 */
                result = add_label(&ins, offset, next_offset, line, segment);
                break;
            case MOTION_CMD_IF:
                if (s_if_count >= MOTION_MAX_FLOW_BLOCKS ||
                    if_depth >= MOTION_MAX_FLOW_DEPTH) {
                    result = MOTION_ERR_MEMORY;
                    break;
                }
                /* 先登记 IF 起点，待 ELSE/END 出现时回填跳转目标 */
                memset(&s_ifs[s_if_count], 0, sizeof(s_ifs[s_if_count]));
                s_ifs[s_if_count].if_offset = offset;
                s_ifs[s_if_count].segment = segment;
                open_if[if_depth++] = s_if_count;
                s_if_count++;
                break;
            case MOTION_CMD_ELSE:
                if (if_depth == 0U) {
                    result = MOTION_ERR_FLOW; /* ELSE 没有对应的 IF */
                    break;
                }
                /* ELSE 归属于最近的未闭合 IF */
                s_ifs[open_if[if_depth - 1U]].else_offset = offset;
                s_ifs[open_if[if_depth - 1U]].else_next = next_offset;
                s_ifs[open_if[if_depth - 1U]].has_else = true;
                break;
            case MOTION_CMD_END:
                if (if_depth == 0U) {
                    result = MOTION_ERR_FLOW; /* END 没有对应的 IF */
                    break;
                }
                /* 弹栈并回填 END 位置 */
                if_depth--;
                s_ifs[open_if[if_depth]].end_offset = offset;
                s_ifs[open_if[if_depth]].end_next = next_offset;
                break;
            case MOTION_CMD_LOOP:
                if (s_loop_count >= MOTION_MAX_FLOW_BLOCKS ||
                    loop_depth >= MOTION_MAX_FLOW_DEPTH) {
                    result = MOTION_ERR_MEMORY;
                    break;
                }
                /* 记录 LOOP 起点，循环体从下一行开始 */
                memset(&s_loops[s_loop_count], 0,
                       sizeof(s_loops[s_loop_count]));
                s_loops[s_loop_count].loop_offset = offset;
                s_loops[s_loop_count].body_offset = next_offset;
                s_loops[s_loop_count].segment = segment;
                open_loop[loop_depth++] = s_loop_count;
                s_loop_count++;
                break;
            case MOTION_CMD_ENDLOOP:
                if (loop_depth == 0U) {
                    result = MOTION_ERR_FLOW; /* ENDLOOP 没有对应的 LOOP */
                    break;
                }
                /* 弹栈并回填 ENDLOOP 位置 */
                loop_depth--;
                s_loops[open_loop[loop_depth]].endloop_offset = offset;
                s_loops[open_loop[loop_depth]].endloop_next = next_offset;
                break;
            case MOTION_CMD_JOB:
                /* JOB 只能出现在主程序段，且数量受 MOTION_MAX_JOBS 限制 */
                if (segment != 0 || s_job_count >= MOTION_MAX_JOBS) {
                    result = MOTION_ERR_FLOW;
                    break;
                }
                memset(&s_jobs[s_job_count], 0,
                       sizeof(s_jobs[s_job_count]));
                strncpy(s_jobs[s_job_count].name, ins.name,
                        sizeof(s_jobs[s_job_count].name) - 1U);
                s_jobs[s_job_count].job_offset = offset;
                s_jobs[s_job_count].start_offset = next_offset;
                s_jobs[s_job_count].segment = (int)s_job_count + 1;
                job_index = (int)s_job_count;
                segment = (int)s_job_count + 1; /* 切换到该 JOB 段 */
                s_job_count++;
                break;
            case MOTION_CMD_ENDJOB:
                if (job_index < 0) {
                    result = MOTION_ERR_FLOW; /* ENDJOB 没有对应的 JOB */
                    break;
                }
                /* 回填 JOB 结束位置并回到主程序段 */
                s_jobs[job_index].end_offset = offset;
                s_jobs[job_index].next_offset = next_offset;
                job_index = -1;
                segment = 0;
                break;
            case MOTION_CMD_ON_ERROR:
                /* 每段最多一个处理器；段号可用范围 0..MOTION_MAX_JOBS */
                if (segment < 0 ||
                    segment > (int)MOTION_MAX_JOBS ||
                    s_errors[segment].active) {
                    result = MOTION_ERR_FLOW;
                    break;
                }
                memset(&s_errors[segment], 0,
                       sizeof(s_errors[segment]));
                s_errors[segment].active = true;
                s_errors[segment].on_offset = offset;
                s_errors[segment].handler_offset = next_offset;
                error_segment = segment;
                break;
            case MOTION_CMD_ENDERROR:
                if (error_segment < 0) {
                    result = MOTION_ERR_FLOW; /* ENDERROR 没有对应的 ON ERROR */
                    break;
                }
                /* 回填处理器结束位置，运行到此处表示异常处理完成 */
                s_errors[error_segment].enderror_offset = offset;
                s_errors[error_segment].enderror_next = next_offset;
                error_segment = -1;
                break;
            default:
                break; /* 其余命令无需建索引 */
            }
        }

        if (result != 0) {
            break;
        }

        offset = next_offset;
        line++;
    }

    (void)source.close(&source);

    /* 文件结束时仍有未闭合的块，说明结构不完整 */
    if (result == 0 &&
        (if_depth != 0U || loop_depth != 0U || job_index >= 0 ||
         error_segment >= 0)) {
        result = MOTION_ERR_FLOW;
    }

    return result;
}

/*
 * 查找标签：先按段精确匹配；JOB 段（segment != 0）未命中时
 * 回退到主程序段（segment 0），从而支持 JOB 调用全局标签。
 * 返回只读表项指针，未找到返回 NULL。
 */
const motion_label_t *motion_flow_find_label(const char *name, int segment)
{
    uint16_t i;

    if (name == NULL || s_labels == NULL) {
        return NULL;
    }

    for (i = 0U; i < s_label_count; i++) {
        if (s_labels[i].segment == segment &&
            strcmp(s_labels[i].name, name) == 0) {
            return &s_labels[i];
        }
    }

    if (segment != 0) {
        for (i = 0U; i < s_label_count; i++) {
            if (s_labels[i].segment == 0 &&
                strcmp(s_labels[i].name, name) == 0) {
                return &s_labels[i];
            }
        }
    }

    return NULL;
}

/* 按 IF 指令偏移查找 IF 块索引；未命中返回 NULL */
const motion_if_entry_t *motion_flow_find_if(uint32_t offset)
{
    uint16_t i;

    if (s_ifs == NULL) {
        return NULL;
    }

    for (i = 0U; i < s_if_count; i++) {
        if (s_ifs[i].if_offset == offset) {
            return &s_ifs[i];
        }
    }
    return NULL;
}

/* 按 ELSE 指令偏移查找其所属 IF 块索引；未命中返回 NULL */
const motion_if_entry_t *motion_flow_find_else(uint32_t offset)
{
    uint16_t i;

    if (s_ifs == NULL) {
        return NULL;
    }

    for (i = 0U; i < s_if_count; i++) {
        if (s_ifs[i].has_else && s_ifs[i].else_offset == offset) {
            return &s_ifs[i];
        }
    }
    return NULL;
}

/* 按 LOOP 指令偏移查找 LOOP 块索引；未命中返回 NULL */
const motion_loop_entry_t *motion_flow_find_loop(uint32_t offset)
{
    uint16_t i;

    if (s_loops == NULL) {
        return NULL;
    }

    for (i = 0U; i < s_loop_count; i++) {
        if (s_loops[i].loop_offset == offset) {
            return &s_loops[i];
        }
    }
    return NULL;
}

/* 按 ON ERROR 指令偏移查找处理器索引；未命中返回 NULL */
const motion_error_entry_t *motion_flow_find_error(uint32_t offset)
{
    uint16_t i;

    if (s_errors == NULL) {
        return NULL;
    }

    for (i = 0U; i <= MOTION_MAX_JOBS; i++) {
        if (s_errors[i].active && s_errors[i].on_offset == offset) {
            return &s_errors[i];
        }
    }
    return NULL;
}

/*
 * 取指定段的异常处理器：优先本段，JOB 段无处理器时回退到段 0。
 * 段号越界或对应段未定义处理器时返回 NULL。
 */
const motion_error_entry_t *motion_flow_find_handler(int segment)
{
    if (s_errors == NULL) {
        return NULL;
    }
    if (segment >= 0 && segment <= (int)MOTION_MAX_JOBS &&
        s_errors[segment].active) {
        return &s_errors[segment];
    }
    if (segment != 0 && s_errors[0].active) {
        return &s_errors[0];
    }
    return NULL;
}

/* 按名称查找 JOB 索引；未命中返回 NULL */
const motion_job_entry_t *motion_flow_find_job(const char *name)
{
    uint16_t i;

    if (name == NULL || s_jobs == NULL) {
        return NULL;
    }

    for (i = 0U; i < s_job_count; i++) {
        if (strcmp(s_jobs[i].name, name) == 0) {
            return &s_jobs[i];
        }
    }
    return NULL;
}

/* 按 JOB 指令偏移查找 JOB 索引；未命中返回 NULL */
const motion_job_entry_t *motion_flow_find_job_by_offset(uint32_t offset)
{
    uint16_t i;

    if (s_jobs == NULL) {
        return NULL;
    }

    for (i = 0U; i < s_job_count; i++) {
        if (s_jobs[i].job_offset == offset) {
            return &s_jobs[i];
        }
    }
    return NULL;
}

/* 返回已登记的 JOB 总数 */
uint8_t motion_flow_job_count(void)
{
    return (uint8_t)s_job_count;
}

/* 按序号取 JOB 索引；index 越界返回 NULL */
const motion_job_entry_t *motion_flow_job_at(uint8_t index)
{
    if (s_jobs == NULL || index >= s_job_count) {
        return NULL;
    }
    return &s_jobs[index];
}

/*
 * 由字节偏移反查行号：遍历行首偏移表逐个比对。
 * 返回行号（从 1 开始），未命中返回 0。
 */
uint32_t motion_flow_line_at(uint32_t offset)
{
    uint16_t i;

    if (s_line_offsets == NULL) {
        return 0U;
    }

    for (i = 0U; i < s_line_count; i++) {
        if (s_line_offsets[i] == offset) {
            return (uint32_t)i + 1U;
        }
    }
    return 0U;
}

/* ------------------------------------------------------------------ */
/* 表达式求值                                                          */
/* ------------------------------------------------------------------ */

/* 求值器状态：游标、变量表与错误码（0 表示尚无错误） */
typedef struct {
    const char *p;
    const motion_variable_table_t *variables;
    int error;
} eval_t;

/* 跳过空白字符（仅空格与制表符，不含换行） */
static void e_skip(eval_t *e)
{
    while (*e->p == ' ' || *e->p == '\t') {
        e->p++;
    }
}

/* 字符转大写，用于关键字不区分大小写匹配 */
static char e_upper(char c)
{
    if (c >= 'a' && c <= 'z') {
        return (char)(c - 'a' + 'A');
    }
    return c;
}

/* 判断字符是否可作为标识符/关键字的一部分 */
static bool e_is_name_char(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') || c == '_' || c == '.';
}

/*
 * 匹配一个关键字（如 AND、OR、NOT），大小写不敏感。
 * 返回 true 表示匹配成功并推进游标；否则不改动游标。
 * 注意：关键字后必须是非标识符字符，避免把 ANDROID 误判为 AND。
 */
static bool e_match_word(eval_t *e, const char *word)
{
    size_t i;
    const char *p;

    e_skip(e);
    p = e->p;

    for (i = 0U; word[i] != '\0'; i++) {
        if (e_upper(p[i]) != e_upper(word[i])) {
            return false;
        }
    }

    if (e_is_name_char(p[i])) {
        return false;
    }

    e->p = p + i;
    return true;
}

/* 匹配一个运算符符号；成功则推进游标，失败不改动游标 */
static bool e_match_op(eval_t *e, const char *op)
{
    size_t len;

    e_skip(e);
    len = strlen(op);
    if (strncmp(e->p, op, len) != 0) {
        return false;
    }
    e->p += len;
    return true;
}

/* 构造整数值 */
static motion_value_t e_make_int(int64_t value)
{
    motion_value_t v;

    memset(&v, 0, sizeof(v));
    v.type = MOTION_VALUE_INTEGER;
    v.data.integer = value;
    return v;
}

/* 构造布尔值 */
static motion_value_t e_make_bool(bool value)
{
    motion_value_t v;

    memset(&v, 0, sizeof(v));
    v.type = MOTION_VALUE_BOOLEAN;
    v.data.boolean = value;
    return v;
}

/* 前向声明：括号内需重新从最低优先级开始递归求值 */
static motion_value_t e_or(eval_t *e);

/*
 * 原子项：括号表达式、字面量或变量引用。
 * 变量引用会通过变量表解析为实际值；解析失败置 MOTION_ERR_VARIABLE。
 * 返回求值结果；出错时返回整数 0 并设置 e->error。
 */
static motion_value_t e_primary(eval_t *e)
{
    motion_value_t v;

    memset(&v, 0, sizeof(v));
    e_skip(e);

    if (*e->p == '(') {
        e->p++;
        v = e_or(e);
        e_skip(e);
        if (*e->p != ')') {
            e->error = MOTION_ERR_SYNTAX; /* 括号不闭合 */
            return e_make_int(0);
        }
        e->p++;
        return v;
    }

    if (motion_parser_value_token(&e->p, &v) != 0) {
        e->error = MOTION_ERR_SYNTAX;
        return e_make_int(0);
    }

    if (v.type == MOTION_VALUE_VARIABLE) {
        motion_value_t resolved;

        if (!motion_variable_get(e->variables, v.data.variable, &resolved)) {
            e->error = MOTION_ERR_VARIABLE; /* 变量未定义 */
            return e_make_int(0);
        }
        return resolved;
    }

    return v;
}

/* 判断值是否可按整数参与运算（整数与布尔均视为整型） */
static bool e_is_int(const motion_value_t *v)
{
    return v->type == MOTION_VALUE_INTEGER || v->type == MOTION_VALUE_BOOLEAN;
}

/*
 * 二元数值运算。
 * 参数：e 求值器、a/b 操作数、op 运算符（+ - * / %）。
 * 返回：整型操作数在 + - * 下保持整数结果，否则转为浮点；
 *       除数为 0 时置 MOTION_ERR_RUNTIME 并返回 0。
 * 注意：整数除法 / 与取模 % 均为整型语义，不做隐式浮点提升。
 */
static motion_value_t e_binary_numeric(eval_t *e, motion_value_t a,
                                       motion_value_t b, char op)
{
    bool ints = e_is_int(&a) && e_is_int(&b);

    if (op == '/' || op == '%') {
        if (op == '/' && ints) {
            int64_t divisor = motion_value_to_int(&b);

            if (divisor == 0) {
                e->error = MOTION_ERR_RUNTIME;
                return e_make_int(0);
            }
            return e_make_int(motion_value_to_int(&a) / divisor);
        }
        if (op == '%') {
            int64_t divisor = motion_value_to_int(&b);

            if (divisor == 0) {
                e->error = MOTION_ERR_RUNTIME;
                return e_make_int(0);
            }
            return e_make_int(motion_value_to_int(&a) % divisor);
        }
        if (motion_value_to_double(&b) == 0.0) {
            e->error = MOTION_ERR_RUNTIME;
            return e_make_int(0);
        }
    }

    if (ints && op != '/') {
        int64_t ia = motion_value_to_int(&a);
        int64_t ib = motion_value_to_int(&b);

        /* 整型 + - * 保持整数，避免转换为浮点造成精度损失 */
        switch (op) {
        case '+':
            return e_make_int(ia + ib);
        case '-':
            return e_make_int(ia - ib);
        case '*':
            return e_make_int(ia * ib);
        default:
            break;
        }
    }

    {
        /* 至少一侧为浮点（或浮点除法）时按 double 计算 */
        double da = motion_value_to_double(&a);
        double db = motion_value_to_double(&b);
        motion_value_t result;

        memset(&result, 0, sizeof(result));
        result.type = MOTION_VALUE_FLOAT;

        switch (op) {
        case '+':
            result.data.real = da + db;
            break;
        case '-':
            result.data.real = da - db;
            break;
        case '*':
            result.data.real = da * db;
            break;
        case '/':
            result.data.real = da / db;
            break;
        default:
            result.data.real = 0.0;
            break;
        }
        return result;
    }
}

/* 前向声明：乘除层的操作数由一元层解析 */
static motion_value_t e_unary(eval_t *e);

/* 乘除模层：左结合循环处理 * / %，实际运算交给 e_binary_numeric */
static motion_value_t e_mul(eval_t *e)
{
    motion_value_t left = e_unary(e);

    while (e->error == 0) {
        if (e_match_op(e, "*")) {
            left = e_binary_numeric(e, left, e_unary(e), '*');
        } else if (e_match_op(e, "/")) {
            left = e_binary_numeric(e, left, e_unary(e), '/');
        } else if (e_match_op(e, "%")) {
            left = e_binary_numeric(e, left, e_unary(e), '%');
        } else {
            break;
        }
    }

    return left;
}

/* 单目层：处理前置负号与正号，支持连续出现（如 --x） */
static motion_value_t e_unary(eval_t *e)
{
    if (e_match_op(e, "-")) {
        motion_value_t v = e_unary(e);

        if (e_is_int(&v)) {
            return e_make_int(-motion_value_to_int(&v));
        }
        {
            motion_value_t result;

            memset(&result, 0, sizeof(result));
            result.type = MOTION_VALUE_FLOAT;
            result.data.real = -motion_value_to_double(&v);
            return result;
        }
    }
    if (e_match_op(e, "+")) {
        return e_unary(e);
    }
    return e_primary(e);
}

/* 加减层：左结合循环处理 + 与 - */
static motion_value_t e_add(eval_t *e)
{
    motion_value_t left = e_mul(e);

    while (e->error == 0) {
        if (e_match_op(e, "+")) {
            left = e_binary_numeric(e, left, e_mul(e), '+');
        } else if (e_match_op(e, "-")) {
            left = e_binary_numeric(e, left, e_mul(e), '-');
        } else {
            break;
        }
    }

    return left;
}

/* 逻辑非层：NOT 为右结合，可连续使用；否则交给加减层 */
static motion_value_t e_not(eval_t *e)
{
    if (e_match_word(e, "NOT")) {
        motion_value_t value = e_not(e);

        return e_make_bool(!motion_value_to_bool(&value));
    }
    return e_add(e);
}

/*
 * 比较层：识别 == != > >= < <=，两侧各解析一个 e_not 操作数。
 * 比较规则：
 *   - 两侧均为字符串时按 strcmp 字典序比较；
 *   - 两侧均为整型（含布尔）时按 int64 比较；
 *   - 其余情况按 double 比较。
 * 无比较运算符时直接返回左值；右操作数求值出错则返回 false。
 */
static motion_value_t e_compare(eval_t *e)
{
    motion_value_t left = e_not(e);
    motion_compare_t op = MOTION_CMP_NONE;
    motion_value_t right;

    if (e->error != 0) {
        return left;
    }

    if (e_match_op(e, "==")) {
        op = MOTION_CMP_EQ;
    } else if (e_match_op(e, "!=")) {
        op = MOTION_CMP_NE;
    } else if (e_match_op(e, ">=")) {
        op = MOTION_CMP_GE;
    } else if (e_match_op(e, "<=")) {
        op = MOTION_CMP_LE;
    } else if (e_match_op(e, ">")) {
        op = MOTION_CMP_GT;
    } else if (e_match_op(e, "<")) {
        op = MOTION_CMP_LT;
    }

    if (op == MOTION_CMP_NONE) {
        return left; /* 只是普通数值，没有比较运算 */
    }

    right = e_not(e);
    if (e->error != 0) {
        return e_make_bool(false);
    }

    if (left.type == MOTION_VALUE_STRING && right.type == MOTION_VALUE_STRING) {
        /* 字符串按字典序比较 */
        int cmp = strcmp(left.data.string, right.data.string);

        switch (op) {
        case MOTION_CMP_EQ:
            return e_make_bool(cmp == 0);
        case MOTION_CMP_NE:
            return e_make_bool(cmp != 0);
        case MOTION_CMP_GT:
            return e_make_bool(cmp > 0);
        case MOTION_CMP_GE:
            return e_make_bool(cmp >= 0);
        case MOTION_CMP_LT:
            return e_make_bool(cmp < 0);
        case MOTION_CMP_LE:
            return e_make_bool(cmp <= 0);
        default:
            return e_make_bool(false);
        }
    }

    if (e_is_int(&left) && e_is_int(&right)) {
        /* 整型精确比较，避免浮点误差 */
        int64_t a = motion_value_to_int(&left);
        int64_t b = motion_value_to_int(&right);

        switch (op) {
        case MOTION_CMP_EQ:
            return e_make_bool(a == b);
        case MOTION_CMP_NE:
            return e_make_bool(a != b);
        case MOTION_CMP_GT:
            return e_make_bool(a > b);
        case MOTION_CMP_GE:
            return e_make_bool(a >= b);
        case MOTION_CMP_LT:
            return e_make_bool(a < b);
        case MOTION_CMP_LE:
            return e_make_bool(a <= b);
        default:
            return e_make_bool(false);
        }
    }

    {
        /* 混合类型或浮点情况统一按 double 比较 */
        double a = motion_value_to_double(&left);
        double b = motion_value_to_double(&right);

        switch (op) {
        case MOTION_CMP_EQ:
            return e_make_bool(a == b);
        case MOTION_CMP_NE:
            return e_make_bool(a != b);
        case MOTION_CMP_GT:
            return e_make_bool(a > b);
        case MOTION_CMP_GE:
            return e_make_bool(a >= b);
        case MOTION_CMP_LT:
            return e_make_bool(a < b);
        case MOTION_CMP_LE:
            return e_make_bool(a <= b);
        default:
            return e_make_bool(false);
        }
    }
}

/* 逻辑与层：左结合、不短路求值（两侧都会求值），转换为布尔后取 AND */
static motion_value_t e_and(eval_t *e)
{
    motion_value_t left = e_compare(e);

    while (e->error == 0 && e_match_word(e, "AND")) {
        bool a = motion_value_to_bool(&left);
        motion_value_t right = e_compare(e);
        bool b = motion_value_to_bool(&right);

        left = e_make_bool(a && b);
    }

    return left;
}

/* 逻辑或层（最低优先级）：左结合、不短路求值，转换为布尔后取 OR */
static motion_value_t e_or(eval_t *e)
{
    motion_value_t left = e_and(e);

    while (e->error == 0 && e_match_word(e, "OR")) {
        bool a = motion_value_to_bool(&left);
        motion_value_t right = e_and(e);
        bool b = motion_value_to_bool(&right);

        left = e_make_bool(a || b);
    }

    return left;
}

/*
 * 对整条表达式求值。
 * 参数：expression 表达式文本；variables 变量表；result 输出结果。
 * 返回：0 成功；负数表示错误码的相反数（MOTION_ERR_PARAMETER /
 *       MOTION_ERR_SYNTAX / MOTION_ERR_VARIABLE / MOTION_ERR_RUNTIME）。
 * 注意：求值完成后必须恰好到达字符串结尾，残留字符视为语法错误。
 */
int motion_flow_eval(const char *expression,
                     const motion_variable_table_t *variables,
                     motion_value_t *result)
{
    eval_t e;
    motion_value_t value;

    if (expression == NULL || result == NULL) {
        return MOTION_ERR_PARAMETER;
    }

    e.p = expression;
    e.variables = variables;
    e.error = 0;

    value = e_or(&e);
    e_skip(&e);

    if (e.error == 0 && *e.p != '\0') {
        e.error = MOTION_ERR_SYNTAX; /* 表达式后仍有无法解析的残留字符 */
    }

    if (e.error != 0) {
        return -e.error; /* 以负值返回错误码 */
    }

    *result = value;
    return 0;
}
