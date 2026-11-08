#include "motion_parser.h"

/*
 * motion_parser.c —— MotionScript 行级语法解析实现。
 *
 * 模块职责：
 *   1) 词法辅助：跳过空白、去行尾空白、剥离注释、读取"单词/名字/设备号"；
 *   2) 数值与单位：解析整数/浮点/布尔/字符串/$变量，并把 s、cm、kHz 等
 *      单位统一换算成基准单位（ms、mm、Hz）；
 *   3) 指令分支：设备指令（STEP/PWM/GPIO/ADC/SENSOR/RELAY）、WAIT、
 *      SET、IF/ELSE/END、LOOP、标签跳转、错误处理、JOB 系列；
 *   4) 参数子句：解析形如 "KEY VALUE, KEY VALUE" 的键值对。
 *
 * 数据流：输入 char* 行 -> 预处理（去注释/去尾空白）-> read_word 取命令字
 *         -> 各 parse_xxx_line 填充 motion_instruction_t -> 调用方执行。
 *
 * 边界约束：
 *   - 栈上临时缓冲均为定长：行缓冲 MOTION_MAX_LINE，命令字 24 字节，
 *     动作/子动作 16 字节，单位 12 字节；
 *   - 所有字符串写入都用 strncpy + 手工补 '\0'，防止截断后越界；
 *   - 参数数量超过 MOTION_MAX_PARAMS 返回 MOTION_PARSE_OVERFLOW；
 *   - 本文件不分配内存、不保存任何静态状态，可重入。
 */

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* 基础词法工具                                                        */
/* ------------------------------------------------------------------ */

/* 将小写字母转大写，其他字符原样返回（仅处理 ASCII）。 */
static char char_upper(char c)
{
    if (c >= 'a' && c <= 'z') {
        return (char)(c - 'a' + 'A');
    }
    return c;
}

/* 跳过空格与制表符，返回首个非空白字符位置；p 为 NULL 时返回 NULL。 */
static const char *skip_ws(const char *p)
{
    if (p == NULL) {
        return NULL;
    }
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    return p;
}

/* 原地去掉字符串尾部的空白与换行符（' ' '\t' '\r' '\n'）。 */
static void trim_tail(char *s)
{
    size_t len;

    if (s == NULL) {
        return;
    }
    len = strlen(s);
    while (len > 0U &&
           (s[len - 1U] == ' ' || s[len - 1U] == '\t' ||
            s[len - 1U] == '\r' || s[len - 1U] == '\n')) {
        s[--len] = '\0';
    }
}

/* 原地剥离行注释：支持 ';' 与 '//'，但双引号字符串内的内容不视为注释。
 * 注释起始处直接写入 '\0'，等价于截断该行。 */
static void strip_comment(char *s)
{
    bool in_string = false; /* 是否处于双引号字符串内部 */
    size_t i;

    if (s == NULL) {
        return;
    }

    for (i = 0U; s[i] != '\0'; i++) {
        /* 引号成对切换，用于保护字符串里的 ';' 或 '//' */
        if (s[i] == '"') {
            in_string = !in_string;
            continue;
        }
        if (in_string) {
            continue;
        }
        if (s[i] == ';') {
            s[i] = '\0';
            return;
        }
        if (s[i] == '/' && s[i + 1U] == '/') {
            s[i] = '\0';
            return;
        }
    }
}

/* 读取一个以空白/逗号为界的单词并转大写。
 * 参数：cursor 输入输出游标；out 输出缓冲；size 缓冲容量。
 * 返回：0 成功；-1 参数错误或当前位置没有有效单词。
 * 说明：超出缓冲的字符会被丢弃，但仍会推进游标，保证后续解析对齐。 */
static int read_word(const char **cursor, char *out, size_t size)
{
    const char *p;
    size_t i = 0U;

    if (cursor == NULL || *cursor == NULL || out == NULL || size == 0U) {
        return -1;
    }

    p = skip_ws(*cursor);
    if (*p == '\0' || *p == ',') {
        return -1;
    }

    while (*p != '\0' && *p != ',' && *p != ' ' && *p != '\t') {
        if ((i + 1U) < size) {
            out[i++] = char_upper(*p);
        }
        p++;
    }
    out[i] = '\0';
    *cursor = p;
    return (i > 0U) ? 0 : -1;
}

/* 判断字符是否可作为名字（标签名、变量名、JOB 名）的组成部分。 */
static bool is_name_char(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') || c == '_' || c == '.';
}

/* 读取一个名字（允许字母、数字、'_'、'.'）并统一转为大写。
 * 参数：cursor 输入输出游标；out 输出缓冲；size 缓冲容量。
 * 返回：0 成功；-1 参数错误或名字为空。
 * 说明：名字大小写不敏感；超出缓冲部分被截断。 */
static int read_name(const char **cursor, char *out, size_t size)
{
    const char *p;
    size_t i = 0U;

    if (cursor == NULL || *cursor == NULL || out == NULL || size == 0U) {
        return -1;
    }

    p = *cursor;
    while (is_name_char(*p)) {
        if ((i + 1U) < size) {
            out[i++] = char_upper(*p);
        }
        p++;
    }
    out[i] = '\0';
    if (i == 0U) {
        return -1;
    }
    *cursor = p;
    return 0;
}

/* 读取整数设备号。
 * 参数：cursor 输入输出游标；id 输出设备号。
 * 返回：0 成功；-1 非纯十进制数字或超出 0 ~ 65535。
 * 说明：整个单词必须都是数字，不接受 "3abc" 这类混合形式。 */
static int read_device_id(const char **cursor, uint16_t *id)
{
    char word[24];
    char *end = NULL;
    long value;

    if (read_word(cursor, word, sizeof(word)) != 0) {
        return -1;
    }

    value = strtol(word, &end, 10);
    /* end == word 表示没有任何数字；*end != '\0' 表示存在多余字符 */
    if (end == word || *end != '\0' || value < 0L || value > 65535L) {
        return -1;
    }

    *id = (uint16_t)value;
    return 0;
}

/* ------------------------------------------------------------------ */
/* 单位与数值                                                          */
/* ------------------------------------------------------------------ */

/* 大小写不敏感地比较单位名与名称字符串是否完全相等。 */
static bool unit_equal(const char *unit, const char *name)
{
    size_t i;

    for (i = 0U; unit[i] != '\0' || name[i] != '\0'; i++) {
        if (char_upper(unit[i]) != char_upper(name[i])) {
            return false;
        }
    }
    return true;
}

/* 按倍率原地换算数值。
 * 参数：value 待换算数值；factor 乘数。
 * 说明：整数与浮点都换算；若浮点结果恰好是整数，则降级为整数类型，
 *       避免 2s 这类写法在后续比较中带上无意义的小数。 */
static void value_scale(motion_value_t *value, double factor)
{
    if (value->type == MOTION_VALUE_INTEGER) {
        value->data.integer = (int64_t)((double)value->data.integer * factor);
    } else if (value->type == MOTION_VALUE_FLOAT) {
        double result = value->data.real * factor;
        int64_t integer = (int64_t)result;

        if ((double)integer == result) {
            value->type = MOTION_VALUE_INTEGER;
            value->data.integer = integer;
        } else {
            value->data.real = result;
        }
    }
}

/* 识别单位并做归一化换算。
 * 参数：unit 单位字符串；value 已解析的数值（原地修改）。
 * 返回：true 单位合法；false 单位无法识别（value 保持不变）。
 * 说明：时间统一为 ms，长度统一为 mm，频率统一为 Hz。 */
static bool apply_unit(const char *unit, motion_value_t *value)
{
    if (unit_equal(unit, "ms")) {
        value->unit = MOTION_UNIT_MS;
    } else if (unit_equal(unit, "s")) {
        /* 秒 -> 毫秒 */
        value_scale(value, 1000.0);
        value->unit = MOTION_UNIT_MS;
    } else if (unit_equal(unit, "min")) {
        /* 分 -> 毫秒 */
        value_scale(value, 60000.0);
        value->unit = MOTION_UNIT_MS;
    } else if (unit_equal(unit, "h")) {
        /* 小时 -> 毫秒 */
        value_scale(value, 3600000.0);
        value->unit = MOTION_UNIT_MS;
    } else if (unit_equal(unit, "Hz")) {
        value->unit = MOTION_UNIT_HZ;
    } else if (unit_equal(unit, "kHz")) {
        /* 千赫 -> 赫兹 */
        value_scale(value, 1000.0);
        value->unit = MOTION_UNIT_HZ;
    } else if (unit_equal(unit, "%")) {
        value->unit = MOTION_UNIT_PERCENT;
    } else if (unit_equal(unit, "rpm")) {
        value->unit = MOTION_UNIT_RPM;
    } else if (unit_equal(unit, "rps")) {
        value->unit = MOTION_UNIT_RPS;
    } else if (unit_equal(unit, "step/s")) {
        value->unit = MOTION_UNIT_STEP_S;
    } else if (unit_equal(unit, "mm/s")) {
        value->unit = MOTION_UNIT_MM_S;
    } else if (unit_equal(unit, "deg/s")) {
        value->unit = MOTION_UNIT_DEG_S;
    } else if (unit_equal(unit, "deg")) {
        value->unit = MOTION_UNIT_DEG;
    } else if (unit_equal(unit, "mm")) {
        value->unit = MOTION_UNIT_MM;
    } else if (unit_equal(unit, "cm")) {
        /* 厘米 -> 毫米 */
        value_scale(value, 10.0);
        value->unit = MOTION_UNIT_MM;
    } else if (unit_equal(unit, "m")) {
        /* 米 -> 毫米 */
        value_scale(value, 1000.0);
        value->unit = MOTION_UNIT_MM;
    } else {
        return false;
    }

    return true;
}

/* 解析数值字面量及其后紧跟的可选单位。
 * 参数：cursor 输入输出游标；value 输出数值（类型/单位已归一化）。
 * 返回：0 成功；-1 起始处不是合法数值。
 * 说明：先按 strtod 解析，再根据是否出现 '.'/'e'/'E' 决定整数或浮点；
 *       若单位无法识别则把游标回退到数字结尾，让上层继续按其他语法处理。 */
static int parse_number(const char **cursor, motion_value_t *value)
{
    const char *start;
    const char *p;
    char *end = NULL;
    bool is_float = false;
    double real;
    long long integer;

    start = *cursor;
    p = start;

    /* 允许显式正负号，但符号后必须是数字或小数点 */
    if (*p == '+' || *p == '-') {
        p++;
    }
    if (!(*p >= '0' && *p <= '9') && *p != '.') {
        return -1;
    }

    real = strtod(start, &end);
    if (end == start) {
        return -1;
    }

    /* 判断是不是整数字面量 */
    for (p = start; p < end; p++) {
        if (*p == '.' || *p == 'e' || *p == 'E') {
            is_float = true;
            break;
        }
    }

    if (!is_float) {
        /* 纯整数走 strtoll，避免大整数在 double 中丢失精度 */
        integer = strtoll(start, &end, 10);
        value->type = MOTION_VALUE_INTEGER;
        value->data.integer = (int64_t)integer;
    } else {
        value->type = MOTION_VALUE_FLOAT;
        value->data.real = real;
    }

    p = end;

    /* 单位必须紧跟在数值后面（允许 step/s 这类带斜杠的单位） */
    if ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') ||
        *p == '%' || *p == '/') {
        const char *unit_start = p;
        char unit[12];
        size_t i = 0U;

        /* 单位由字母、数字、'/'、'%' 组成，遇到其他字符即结束 */
        while (*p != '\0' &&
               ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') ||
                (*p >= '0' && *p <= '9') || *p == '/' || *p == '%')) {
            if ((i + 1U) < sizeof(unit)) {
                unit[i++] = *p;
            }
            p++;
        }
        unit[i] = '\0';

        if (!apply_unit(unit, value)) {
            /* 不是已知单位：回退游标，交由上层按普通标识符处理 */
            p = unit_start;
        }
    }

    *cursor = p;
    return 0;
}

/* 解析一个值 token，支持整数、浮点、布尔、双引号字符串与 $变量。
 * 参数：cursor 输入输出游标；value 输出值（进入时会被清零）。
 * 返回：0 成功；-1 参数为空、空 token 或语法不合法。
 * 说明：布尔接受 TRUE/ON 与 FALSE/OFF；字符串超出缓冲部分被截断；
 *       变量名统一转大写以便与变量表做大小写不敏感匹配。 */
int motion_parser_value_token(const char **cursor, motion_value_t *value)
{
    const char *p;

    if (cursor == NULL || *cursor == NULL || value == NULL) {
        return -1;
    }

    memset(value, 0, sizeof(*value));
    value->unit = MOTION_UNIT_NONE;

    p = skip_ws(*cursor);
    if (*p == '\0' || *p == ',') {
        return -1;
    }
    *cursor = p;

    /* $变量引用 */
    if (*p == '$') {
        p++;
        if (read_name(&p, value->data.variable,
                      sizeof(value->data.variable)) != 0) {
            return -1;
        }
        value->type = MOTION_VALUE_VARIABLE;
        *cursor = p;
        return 0;
    }

    /* 双引号字符串：不处理转义，遇到下一个 '"' 即结束 */
    if (*p == '"') {
        size_t i = 0U;

        p++;
        while (*p != '\0' && *p != '"') {
            if ((i + 1U) < sizeof(value->data.string)) {
                value->data.string[i++] = *p;
            }
            p++;
        }
        if (*p != '"') {
            return -1; /* 缺少结束引号 */
        }
        p++;
        value->data.string[i] = '\0';
        value->type = MOTION_VALUE_STRING;
        *cursor = p;
        return 0;
    }

    /* 数值（可带正负号、小数点） */
    if (*p == '+' || *p == '-' || (*p >= '0' && *p <= '9') || *p == '.') {
        if (parse_number(cursor, value) != 0) {
            return -1;
        }
        return 0;
    }

    /* 布尔 / 标识符 */
    {
        char word[16];
        const char *save = p;
        size_t i = 0U;

        while (*p != '\0' && *p != ',' && *p != ' ' && *p != '\t') {
            if ((i + 1U) < sizeof(word)) {
                word[i++] = *p;
            }
            p++;
        }
        word[i] = '\0';

        if (unit_equal(word, "TRUE") || unit_equal(word, "ON")) {
            value->type = MOTION_VALUE_BOOLEAN;
            value->data.boolean = true;
            *cursor = p;
            return 0;
        }
        if (unit_equal(word, "FALSE") || unit_equal(word, "OFF")) {
            value->type = MOTION_VALUE_BOOLEAN;
            value->data.boolean = false;
            *cursor = p;
            return 0;
        }

        (void)save; /* 非布尔标识符：不推进游标，交由上层判定 */
        return -1;
    }
}

/* 解析独立的单个值文本，要求除空白/逗号外没有多余内容。
 * 参数：text 待解析文本；value 输出值。
 * 返回：0 成功；-1 text 为空、解析失败或存在尾随内容。 */
int motion_parser_parse_value(const char *text, motion_value_t *value)
{
    const char *p;

    if (text == NULL) {
        return -1;
    }

    p = text;
    if (motion_parser_value_token(&p, value) != 0) {
        return -1;
    }

    p = skip_ws(p);
    return (*p == '\0' || *p == ',') ? 0 : -1;
}

/* ------------------------------------------------------------------ */
/* 参数与默认单位                                                      */
/* ------------------------------------------------------------------ */

/* 按"指令 + 动作 + 参数名"推断缺省单位。
 * 参数：command 指令类型；action 动作名；key 参数名（大写）。
 * 返回：推断出的单位；无法推断时返回 MOTION_UNIT_NONE。
 * 说明：仅在参数未显式书写单位时调用，让 "SPEED 1000" 这类写法
 *       也能按语义（step/s、Hz、% 等）解释。 */
static motion_unit_t default_unit(motion_cmd_t command, const char *action,
                                 const char *key)
{
    if (key == NULL) {
        return MOTION_UNIT_NONE;
    }

    /* 通用时间类参数：TIMEOUT/DEC/DECEL/ACCEL/ACC/TIME 缺省为毫秒 */
    if (strcmp(key, "TIMEOUT") == 0 || strcmp(key, "DEC") == 0 ||
        strcmp(key, "DECEL") == 0 || strcmp(key, "ACCEL") == 0 ||
        strcmp(key, "ACC") == 0 || strcmp(key, "TIME") == 0) {
        return MOTION_UNIT_MS;
    }

    if (command == MOTION_CMD_STEP) {
        if (strcmp(key, "SPEED") == 0) {
            return MOTION_UNIT_STEP_S; /* 步进速度缺省为 步/秒 */
        }
    } else if (command == MOTION_CMD_PWM) {
        if (strcmp(key, "FREQ") == 0) {
            return MOTION_UNIT_HZ; /* PWM 频率缺省为 Hz */
        }
        if (strcmp(key, "DUTY") == 0) {
            return MOTION_UNIT_PERCENT; /* PWM 占空比缺省为 % */
        }
    } else if (command == MOTION_CMD_WAIT) {
        if (strcmp(key, "VALUE") == 0 && action != NULL &&
            strcmp(action, "TIME") == 0) {
            return MOTION_UNIT_MS; /* WAIT TIME 的 VALUE 缺省为毫秒 */
        }
    }

    return MOTION_UNIT_NONE;
}

/* 向指令追加一个参数键值对。
 * 参数：instruction 目标指令；name 参数名；value 参数值。
 * 返回：0 成功；-1 参数数量已达 MOTION_MAX_PARAMS。
 * 说明：参数名会被截断到 15 字节并强制以 '\0' 结尾。 */
static int add_param(motion_instruction_t *instruction, const char *name,
                     const motion_value_t *value)
{
    motion_param_t *param;

    if (instruction->param_count >= MOTION_MAX_PARAMS) {
        return -1;
    }

    param = &instruction->params[instruction->param_count];
    memset(param, 0, sizeof(*param));
    strncpy(param->name, name, sizeof(param->name) - 1U);
    param->name[sizeof(param->name) - 1U] = '\0';
    param->value = *value;
    instruction->param_count++;
    return 0;
}

/* 解析逗号分隔的参数子句，形如 "SPEED 1000rpm, ACCEL 2s"。
 * 参数：p 游标位置；instruction 目标指令（追加参数）。
 * 返回：MOTION_PARSE_OK 结束；INVALID 语法错误；OVERFLOW 参数超限。
 * 说明：允许空子句（直接 '\0'）与多余逗号；解析结束后必须到达行尾。 */
static motion_parse_result_t parse_param_clauses(const char *p,
                                                 motion_instruction_t *instruction)
{
    for (;;) {
        char key[24];
        motion_value_t value;
        motion_unit_t unit;

        p = skip_ws(p);
        if (*p == '\0') {
            return MOTION_PARSE_OK; /* 正常结束 */
        }
        if (*p == ',') {
            p++; /* 容忍连续逗号或多余逗号 */
            continue;
        }

        if (read_word(&p, key, sizeof(key)) != 0) {
            return MOTION_PARSE_INVALID; /* 参数名缺失 */
        }

        p = skip_ws(p);
        if (motion_parser_value_token(&p, &value) != 0) {
            return MOTION_PARSE_INVALID; /* 参数值缺失或非法 */
        }

        /* 未显式写单位时按参数名推断缺省单位 */
        if (value.unit == MOTION_UNIT_NONE) {
            unit = default_unit(instruction->command, instruction->action, key);
            if (unit != MOTION_UNIT_NONE) {
                value.unit = unit;
            }
        }

        if (add_param(instruction, key, &value) != 0) {
            return MOTION_PARSE_OVERFLOW;
        }

        p = skip_ws(p);
        /* 一个键值对之后只允许行尾或逗号 */
        if (*p != '\0' && *p != ',') {
            return MOTION_PARSE_INVALID;
        }
    }
}

/* ------------------------------------------------------------------ */
/* 设备指令                                                            */
/* ------------------------------------------------------------------ */

/* 解析各类设备指令的公共部分：设备号 + 动作名 + 可选的第一个内联值 + 参数子句。
 * 参数：p 命令字之后的游标；command 已识别的指令类型；instruction 输出指令。
 * 返回：解析结果码。
 * 说明：
 *   - STEP N POSITION SET/READ 有二级子动作，SET 后可直接跟一个数值；
 *   - 其他指令动作名之后若紧跟非逗号内容，则视为不带参数名的内联值，
 *     以 "VALUE" 作为参数名保存（如 "PWM 0 START 1000"）；
 *   - 剩余逗号分隔部分交给 parse_param_clauses 处理。 */
static motion_parse_result_t parse_device_line(const char *p,
                                               motion_cmd_t command,
                                               motion_instruction_t *instruction)
{
    char action[16];
    motion_value_t inline_value;

    instruction->command = command;

    if (read_device_id(&p, &instruction->device_id) != 0) {
        return MOTION_PARSE_INVALID;
    }

    if (read_word(&p, action, sizeof(action)) != 0) {
        return MOTION_PARSE_INVALID;
    }
    strncpy(instruction->action, action, sizeof(instruction->action) - 1U);
    instruction->action[sizeof(instruction->action) - 1U] = '\0';

    p = skip_ws(p);

    if (command == MOTION_CMD_STEP && strcmp(action, "POSITION") == 0) {
        char sub[16];

        /* STEP n POSITION SET|READ：解析二级子动作 */
        if (read_word(&p, sub, sizeof(sub)) != 0) {
            return MOTION_PARSE_INVALID;
        }
        strncpy(instruction->subaction, sub,
                sizeof(instruction->subaction) - 1U);
        instruction->subaction[sizeof(instruction->subaction) - 1U] = '\0';

        p = skip_ws(p);
        if (strcmp(sub, "SET") == 0 && *p != '\0' && *p != ',') {
            /* SET 后可直接跟随目标位置，作为 VALUE 参数 */
            if (motion_parser_value_token(&p, &inline_value) != 0) {
                return MOTION_PARSE_INVALID;
            }
            if (add_param(instruction, "VALUE", &inline_value) != 0) {
                return MOTION_PARSE_OVERFLOW;
            }
            p = skip_ws(p);
            if (*p == ',') {
                p++;
            }
        } else if (strcmp(sub, "READ") != 0 && strcmp(sub, "SET") != 0) {
            return MOTION_PARSE_INVALID; /* 仅支持 SET / READ */
        }
    } else if (*p != '\0' && *p != ',') {
        /* 内联参数，例如 SPEED 1000rpm / DUTY 50% / DUTY 30%, ACCEL 2s */
        if (motion_parser_value_token(&p, &inline_value) != 0) {
            return MOTION_PARSE_INVALID;
        }
        if (add_param(instruction, "VALUE", &inline_value) != 0) {
            return MOTION_PARSE_OVERFLOW;
        }
        p = skip_ws(p);
        if (*p == ',') {
            p++;
        }
    } else if (*p == ',') {
        p++; /* 动作名后直接接逗号：跳过，交给参数子句 */
    }

    return parse_param_clauses(p, instruction);
}

/* ------------------------------------------------------------------ */
/* WAIT 指令                                                           */
/* ------------------------------------------------------------------ */

/* 解析 WAIT 指令。
 * 参数：p "WAIT" 之后的游标；instruction 输出指令。
 * 返回：解析结果码。
 * 语法：WAIT TIME <时长>
 *       WAIT STEP|PWM|GPIO|SENSOR <设备号> [, 参数...]
 *       WAIT ALL [, 参数...]
 *       WAIT JOB <名称> [, 参数...]
 * 说明：WAIT TIME 缺省单位为毫秒；目标关键字区分大小写不敏感（已转大写）。 */
static motion_parse_result_t parse_wait_line(const char *p,
                                             motion_instruction_t *instruction)
{
    char target[16];

    instruction->command = MOTION_CMD_WAIT;

    if (read_word(&p, target, sizeof(target)) != 0) {
        return MOTION_PARSE_INVALID;
    }

    strncpy(instruction->action, target, sizeof(instruction->action) - 1U);
    instruction->action[sizeof(instruction->action) - 1U] = '\0';

    p = skip_ws(p);

    /* WAIT ALL：等待所有已启动设备空闲，后面只能跟参数 */
    if (strcmp(target, "ALL") == 0) {
        if (*p != '\0' && *p != ',') {
            return MOTION_PARSE_INVALID;
        }
        if (*p == ',') {
            p++;
            return parse_param_clauses(p, instruction);
        }
        return MOTION_PARSE_OK;
    }

    /* WAIT JOB <名称>：等待指定任务结束 */
    if (strcmp(target, "JOB") == 0) {
        p = skip_ws(p);
        if (read_name(&p, instruction->name,
                      sizeof(instruction->name)) != 0) {
            return MOTION_PARSE_INVALID;
        }
        p = skip_ws(p);
        if (*p == ',') {
            p++;
            return parse_param_clauses(p, instruction);
        }
        return (*p == '\0') ? MOTION_PARSE_OK : MOTION_PARSE_INVALID;
    }

    /* WAIT TIME <时长>：值直接作为 VALUE 参数 */
    if (strcmp(target, "TIME") == 0) {
        motion_value_t value;

        if (motion_parser_value_token(&p, &value) != 0) {
            return MOTION_PARSE_INVALID;
        }
        if (value.unit == MOTION_UNIT_NONE) {
            value.unit = MOTION_UNIT_MS; /* 默认按毫秒解释 */
        }
        if (add_param(instruction, "VALUE", &value) != 0) {
            return MOTION_PARSE_OVERFLOW;
        }
        p = skip_ws(p);
        if (*p == ',') {
            p++;
            return parse_param_clauses(p, instruction);
        }
        return (*p == '\0') ? MOTION_PARSE_OK : MOTION_PARSE_INVALID;
    }

    /* WAIT STEP|PWM|GPIO|SENSOR <设备号>：等待设备状态 */
    if (strcmp(target, "STEP") == 0 || strcmp(target, "PWM") == 0 ||
        strcmp(target, "GPIO") == 0 || strcmp(target, "SENSOR") == 0) {
        if (read_device_id(&p, &instruction->device_id) != 0) {
            return MOTION_PARSE_INVALID;
        }
        p = skip_ws(p);
        if (*p == ',') {
            p++;
            return parse_param_clauses(p, instruction);
        }
        return (*p == '\0') ? MOTION_PARSE_OK : MOTION_PARSE_INVALID;
    }

    return MOTION_PARSE_INVALID; /* 未知等待目标 */
}

/* ------------------------------------------------------------------ */
/* 顶层解析                                                            */
/* ------------------------------------------------------------------ */

/* 把游标之后的剩余文本原样复制为表达式。
 * 参数：p 起始位置；out 输出缓冲；size 缓冲容量。
 * 返回：无。
 * 说明：表达式不做语法检查（SET/IF/LOOP 留给求值阶段处理），
 *       仅去掉首尾空白并保证以 '\0' 结尾。 */
static void copy_expression(const char *p, char *out, size_t size)
{
    size_t len;

    p = skip_ws(p);
    strncpy(out, p, size - 1U);
    out[size - 1U] = '\0';
    trim_tail(out);
    len = strlen(out);
    (void)len;
}

/* 解析 SET 赋值语句：SET $变量 = 表达式。
 * 参数：p "SET" 之后的游标；instruction 输出指令。
 * 返回：MOTION_PARSE_OK 成功；MOTION_PARSE_INVALID 缺少 '$'、变量名或 '='，
 *       或等号右侧为空。
 * 说明：变量名存入 instruction->name，右侧原文存入 expression 供求值使用。 */
static motion_parse_result_t parse_set_line(const char *p,
                                            motion_instruction_t *instruction)
{
    char name[MOTION_MAX_VAR_NAME];

    instruction->command = MOTION_CMD_SET;

    p = skip_ws(p);
    if (*p != '$') {
        return MOTION_PARSE_INVALID; /* 赋值目标必须以 $ 开头 */
    }
    p++;

    if (read_name(&p, name, sizeof(name)) != 0) {
        return MOTION_PARSE_INVALID;
    }
    strncpy(instruction->name, name, sizeof(instruction->name) - 1U);
    instruction->name[sizeof(instruction->name) - 1U] = '\0';

    p = skip_ws(p);
    if (*p != '=') {
        return MOTION_PARSE_INVALID; /* 缺少赋值号 */
    }
    p++;

    copy_expression(p, instruction->expression,
                    sizeof(instruction->expression));
    if (instruction->expression[0] == '\0') {
        return MOTION_PARSE_INVALID; /* 右侧表达式为空 */
    }

    return MOTION_PARSE_OK;
}

/* 解析一整行源文本为一条指令。
 * 参数：line 以 '\0' 结尾的源行；instruction 输出结构（先整体清零）。
 * 返回：MOTION_PARSE_OK / EMPTY / INVALID / OVERFLOW / UNSUPPORTED。
 * 说明：
 *   - line 长度 >= MOTION_MAX_LINE 直接判为 OVERFLOW，避免截断后语义错误；
 *   - 复制到栈缓冲后再去注释、去尾空白，因此不修改调用方文本；
 *   - 命令关键字大小写不敏感（read_word 已统一转大写）。 */
motion_parse_result_t motion_parser_parse(const char *line,
                                          motion_instruction_t *instruction)
{
    char buffer[MOTION_MAX_LINE]; /* 预处理用栈缓冲，最大 256 字节 */
    char command[24];
    const char *p;

    if (instruction == NULL) {
        return MOTION_PARSE_INVALID;
    }
    memset(instruction, 0, sizeof(*instruction));

    if (line == NULL) {
        return MOTION_PARSE_INVALID;
    }
    if (strlen(line) >= MOTION_MAX_LINE) {
        return MOTION_PARSE_OVERFLOW;
    }

    memset(buffer, 0, sizeof(buffer));
    strncpy(buffer, line, sizeof(buffer) - 1U);
    strip_comment(buffer);
    trim_tail(buffer);

    p = skip_ws(buffer);
    if (*p == '\0') {
        return MOTION_PARSE_EMPTY; /* 空行或整行被注释 */
    }

    if (read_word(&p, command, sizeof(command)) != 0) {
        return MOTION_PARSE_INVALID;
    }

    /* 设备类指令：统一走 parse_device_line */
    if (strcmp(command, "STEP") == 0) {
        return parse_device_line(p, MOTION_CMD_STEP, instruction);
    }
    if (strcmp(command, "PWM") == 0) {
        return parse_device_line(p, MOTION_CMD_PWM, instruction);
    }
    if (strcmp(command, "GPIO") == 0) {
        return parse_device_line(p, MOTION_CMD_GPIO, instruction);
    }
    if (strcmp(command, "ADC") == 0) {
        return parse_device_line(p, MOTION_CMD_ADC, instruction);
    }
    if (strcmp(command, "SENSOR") == 0) {
        return parse_device_line(p, MOTION_CMD_SENSOR, instruction);
    }
    if (strcmp(command, "RELAY") == 0) {
        return parse_device_line(p, MOTION_CMD_RELAY, instruction);
    }
    if (strcmp(command, "WAIT") == 0) {
        return parse_wait_line(p, instruction);
    }
    if (strcmp(command, "SET") == 0) {
        return parse_set_line(p, instruction);
    }
    /* IF：条件表达式整段保存，由求值阶段解析 */
    if (strcmp(command, "IF") == 0) {
        instruction->command = MOTION_CMD_IF;
        copy_expression(p, instruction->expression,
                        sizeof(instruction->expression));
        if (instruction->expression[0] == '\0') {
            return MOTION_PARSE_INVALID;
        }
        return MOTION_PARSE_OK;
    }
    if (strcmp(command, "ELSE") == 0) {
        instruction->command = MOTION_CMD_ELSE;
        return MOTION_PARSE_OK;
    }
    if (strcmp(command, "END") == 0) {
        instruction->command = MOTION_CMD_END;
        return MOTION_PARSE_OK;
    }
    /* LOOP：次数/条件表达式可为空，交由执行层处理 */
    if (strcmp(command, "LOOP") == 0) {
        instruction->command = MOTION_CMD_LOOP;
        copy_expression(p, instruction->expression,
                        sizeof(instruction->expression));
        return MOTION_PARSE_OK;
    }
    if (strcmp(command, "ENDLOOP") == 0) {
        instruction->command = MOTION_CMD_ENDLOOP;
        return MOTION_PARSE_OK;
    }
    if (strcmp(command, "BREAK") == 0) {
        instruction->command = MOTION_CMD_BREAK;
        return MOTION_PARSE_OK;
    }
    /* LABEL / GOTO / CALL：单个名字参数，且不允许尾随内容 */
    if (strcmp(command, "LABEL") == 0 || strcmp(command, "GOTO") == 0 ||
        strcmp(command, "CALL") == 0) {
        char name[MOTION_MAX_LABEL_NAME];

        if (read_word(&p, name, sizeof(name)) != 0) {
            return MOTION_PARSE_INVALID;
        }
        p = skip_ws(p);
        if (*p != '\0') {
            return MOTION_PARSE_INVALID;
        }

        instruction->command = (strcmp(command, "LABEL") == 0)
                                   ? MOTION_CMD_LABEL
                                   : ((strcmp(command, "GOTO") == 0)
                                          ? MOTION_CMD_GOTO
                                          : MOTION_CMD_CALL);
        strncpy(instruction->name, name, sizeof(instruction->name) - 1U);
        instruction->name[sizeof(instruction->name) - 1U] = '\0';
        return MOTION_PARSE_OK;
    }
    if (strcmp(command, "RETURN") == 0) {
        instruction->command = MOTION_CMD_RETURN;
        return MOTION_PARSE_OK;
    }
    /* ERROR <消息字符串> 或 ERROR <整数错误码> */
    if (strcmp(command, "ERROR") == 0) {
        motion_value_t value;

        instruction->command = MOTION_CMD_ERROR;
        if (motion_parser_value_token(&p, &value) != 0) {
            return MOTION_PARSE_INVALID;
        }
        p = skip_ws(p);
        if (*p != '\0') {
            return MOTION_PARSE_INVALID;
        }
        if (value.type == MOTION_VALUE_STRING) {
            if (add_param(instruction, "MSG", &value) != 0) {
                return MOTION_PARSE_OVERFLOW;
            }
        } else if (value.type == MOTION_VALUE_INTEGER) {
            if (add_param(instruction, "CODE", &value) != 0) {
                return MOTION_PARSE_OVERFLOW;
            }
        } else {
            return MOTION_PARSE_INVALID; /* 只接受字符串或整数 */
        }
        return MOTION_PARSE_OK;
    }
    /* ON ERROR：错误处理块开始，必须紧跟 ERROR 且无其他内容 */
    if (strcmp(command, "ON") == 0) {
        char second[16];

        if (read_word(&p, second, sizeof(second)) != 0 ||
            strcmp(second, "ERROR") != 0) {
            return MOTION_PARSE_INVALID;
        }
        p = skip_ws(p);
        if (*p != '\0') {
            return MOTION_PARSE_INVALID;
        }
        instruction->command = MOTION_CMD_ON_ERROR;
        return MOTION_PARSE_OK;
    }
    if (strcmp(command, "ENDERROR") == 0) {
        instruction->command = MOTION_CMD_ENDERROR;
        return MOTION_PARSE_OK;
    }
    /* JOB 有三种形态：
     *   JOB <名称>            -> 定义任务
     *   JOB <名称>, 参数...   -> 定义任务并带参数
     *   JOB <名称> STATE,...  -> 查询任务状态 */
    if (strcmp(command, "JOB") == 0) {
        char name[MOTION_MAX_JOB_NAME];

        p = skip_ws(p);
        if (read_name(&p, name, sizeof(name)) != 0) {
            return MOTION_PARSE_INVALID;
        }
        strncpy(instruction->name, name, sizeof(instruction->name) - 1U);
        instruction->name[sizeof(instruction->name) - 1U] = '\0';

        p = skip_ws(p);
        if (*p == '\0') {
            instruction->command = MOTION_CMD_JOB;
            return MOTION_PARSE_OK;
        }

        if (*p == ',') {
            instruction->command = MOTION_CMD_JOB;
            p++;
            return parse_param_clauses(p, instruction);
        }

        {
            char state[16];

            if (read_word(&p, state, sizeof(state)) != 0 ||
                strcmp(state, "STATE") != 0) {
                return MOTION_PARSE_INVALID;
            }
            instruction->command = MOTION_CMD_JOB_STATE;
            p = skip_ws(p);
            if (*p == ',') {
                p++;
                return parse_param_clauses(p, instruction);
            }
            return (*p == '\0') ? MOTION_PARSE_OK : MOTION_PARSE_INVALID;
        }
    }
    if (strcmp(command, "ENDJOB") == 0) {
        instruction->command = MOTION_CMD_ENDJOB;
        return MOTION_PARSE_OK;
    }
    /* START JOB <名称> / STOP JOB <名称> */
    if (strcmp(command, "START") == 0 || strcmp(command, "STOP") == 0) {
        char second[16];
        char name[MOTION_MAX_JOB_NAME];

        if (read_word(&p, second, sizeof(second)) != 0 ||
            strcmp(second, "JOB") != 0) {
            return MOTION_PARSE_INVALID;
        }
        p = skip_ws(p);
        if (read_name(&p, name, sizeof(name)) != 0) {
            return MOTION_PARSE_INVALID;
        }
        p = skip_ws(p);
        if (*p != '\0') {
            return MOTION_PARSE_INVALID;
        }

        instruction->command = (strcmp(command, "START") == 0)
                                   ? MOTION_CMD_START_JOB
                                   : MOTION_CMD_STOP_JOB;
        strncpy(instruction->name, name, sizeof(instruction->name) - 1U);
        instruction->name[sizeof(instruction->name) - 1U] = '\0';
        return MOTION_PARSE_OK;
    }

    return MOTION_PARSE_UNSUPPORTED;
}

/* 把解析结果码转换成可读文本。
 * 参数：result 解析结果码。
 * 返回：静态常量字符串，调用方不得修改或释放。 */
const char *motion_parse_result_message(motion_parse_result_t result)
{
    switch (result) {
    case MOTION_PARSE_OK:
        return "ok";
    case MOTION_PARSE_EMPTY:
        return "empty line";
    case MOTION_PARSE_INVALID:
        return "invalid syntax";
    case MOTION_PARSE_OVERFLOW:
        return "line or parameter overflow";
    case MOTION_PARSE_UNSUPPORTED:
        return "unsupported instruction";
    default:
        return "unknown parse result";
    }
}
