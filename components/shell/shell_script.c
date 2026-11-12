/*
 * shell_script.c - 脚本解析：文本 → 指令数组（含块配对与静态检查）
 *
 * 采用「编译成线性指令 + 目标下标」的方式：
 *   IF/FOR/WHILE 在解析期算出跳转目标，运行期不需要再扫描块结构。
 *   ON ERROR / ON STOP 编译为「跳过块」+ 记录区间，由 cleanup 调用。
 */

#include "shell_internal.h"

#include <string.h>
#include <stdio.h>

/* 块栈元素 */
typedef struct {
    uint8_t type;
    uint16_t instr;         /* 起始指令下标 */
    uint16_t extra;         /* IF: ELSE 指令 / FOR: FOR_NEXT 指令 */
    uint16_t line;
} block_t;

enum {
    BLK_JOB = 1,
    BLK_IF,
    BLK_FOR,
    BLK_WHILE,
    BLK_LOOP,
    BLK_ERR,
    BLK_STOP
};

typedef struct {
    const char *name;       /* 指向脚本文本 */
    uint16_t name_len;
    uint16_t target;
} label_t;

#define SHELL_LABEL_MAX 32U

/* 记录解析位置，便于把行内参数切出来 */
typedef struct {
    shell_script_t *script;
    shell_err_t *err;
    uint16_t count;
    block_t blocks[SHELL_BLOCK_DEPTH];
    uint8_t depth;
    label_t labels[SHELL_LABEL_MAX];
    uint16_t label_count;
    uint16_t pend[SHELL_INSTR_MAX];      /* 待回填的 BREAK/CONTINUE 指令 */
    uint8_t  pend_level[SHELL_INSTR_MAX];
    uint16_t pend_count;
    bool failed;
} parse_ctx_t;

/* 两段文本的大小写不敏感比较（都不是 C 字符串） */
static bool text_ieq(const char *a, const char *b, uint16_t len)
{
    uint16_t i;

    for (i = 0U; i < len; i++) {
        char ca = a[i];
        char cb = b[i];

        if (ca >= 'a' && ca <= 'z') {
            ca = (char)(ca - 'a' + 'A');
        }
        if (cb >= 'a' && cb <= 'z') {
            cb = (char)(cb - 'a' + 'A');
        }
        if (ca != cb) {
            return false;
        }
    }
    return true;
}

static void parse_fail(parse_ctx_t *ctx, uint16_t line, const char *msg)
{
    if (ctx->failed) {
        return;
    }
    ctx->failed = true;
    if (ctx->err != NULL) {
        ctx->err->code = SHELL_ERR_PARSE;
        ctx->err->line = line;
        snprintf(ctx->err->msg, sizeof(ctx->err->msg), "%s", msg);
    }
}

/* 追加一条指令；返回下标，失败返回 0xFFFF */
static uint16_t emit(parse_ctx_t *ctx, uint8_t op, uint16_t line)
{
    shell_instr_t *ins;

    if (ctx->count >= SHELL_INSTR_MAX) {
        parse_fail(ctx, line, "too many instructions");
        return 0xFFFFU;
    }
    ins = &ctx->script->code[ctx->count];
    memset(ins, 0, sizeof(*ins));
    ins->op = op;
    ins->line = line;
    return ctx->count++;
}

static block_t *top_block(parse_ctx_t *ctx)
{
    return (ctx->depth > 0U) ? &ctx->blocks[ctx->depth - 1U] : NULL;
}

static bool push_block(parse_ctx_t *ctx, uint8_t type, uint16_t instr,
                       uint16_t line)
{
    if (ctx->depth >= SHELL_BLOCK_DEPTH) {
        parse_fail(ctx, line, "block depth exceeded");
        return false;
    }
    ctx->blocks[ctx->depth].type = type;
    ctx->blocks[ctx->depth].instr = instr;
    ctx->blocks[ctx->depth].extra = 0U;
    ctx->blocks[ctx->depth].line = line;
    ctx->depth++;
    return true;
}

/* 找最近的循环块（BREAK/CONTINUE 目标） */
static block_t *find_loop(parse_ctx_t *ctx, int *level)
{
    int i;

    for (i = (int)ctx->depth - 1; i >= 0; i--) {
        uint8_t t = ctx->blocks[i].type;

        if (t == BLK_FOR || t == BLK_WHILE || t == BLK_LOOP) {
            if (level != NULL) {
                *level = i;
            }
            return &ctx->blocks[i];
        }
    }
    return NULL;
}

static void add_label(parse_ctx_t *ctx, const char *name, uint16_t len,
                      uint16_t index, uint16_t line)
{
    uint16_t i;

    for (i = 0U; i < ctx->label_count; i++) {
        if (ctx->labels[i].name_len == len &&
            text_ieq(ctx->labels[i].name, name, len)) {
            parse_fail(ctx, line, "duplicate label");
            return;
        }
    }
    if (ctx->label_count >= SHELL_LABEL_MAX) {
        parse_fail(ctx, line, "too many labels");
        return;
    }
    ctx->labels[ctx->label_count].name = name;
    ctx->labels[ctx->label_count].name_len = len;
    ctx->labels[ctx->label_count].target = index;
    ctx->label_count++;
}

static bool find_label(parse_ctx_t *ctx, const char *name, uint16_t len,
                       uint16_t *target)
{
    uint16_t i;

    for (i = 0U; i < ctx->label_count; i++) {
        if (ctx->labels[i].name_len == len &&
            text_ieq(ctx->labels[i].name, name, len)) {
            *target = ctx->labels[i].target;
            return true;
        }
    }
    return false;
}

/* BREAK/CONTINUE 先登记，循环闭合时按层级回填目标 */
static void pend_add(parse_ctx_t *ctx, uint16_t instr)
{
    if (ctx->pend_count >= SHELL_INSTR_MAX) {
        return;
    }
    ctx->pend[ctx->pend_count] = instr;
    ctx->pend_level[ctx->pend_count] = ctx->depth;
    ctx->pend_count++;
}

static void pend_patch(parse_ctx_t *ctx, uint8_t level,
                       uint16_t break_target, uint16_t cont_target)
{
    uint16_t i;

    for (i = 0U; i < ctx->pend_count; i++) {
        shell_instr_t *ins = &ctx->script->code[ctx->pend[i]];

        /* 已由更内层循环回填的跳过；只处理本层及更深层的 BREAK */
        if (ins->target != 0xFFFFU || ctx->pend_level[i] < level) {
            continue;
        }
        ins->target = (ins->op == SHELL_OP_BREAK) ? break_target : cont_target;
    }
}

/* 取第 n 个 token 之后的参数区间（offset 相对脚本文本） */
static void arg_range(const char *base, const shell_tok_t *toks, uint16_t count,
                      uint16_t first, uint32_t *off, uint16_t *len)
{
    const char *start;
    const char *end;

    if (first >= count || toks[first].type == SHELL_TOK_EOF) {
        *off = 0U;
        *len = 0U;
        return;
    }
    start = toks[first].p;
    end = toks[count - 2U].p + toks[count - 2U].len;   /* 最后一个有效 token 末尾 */
    *off = (uint32_t)(start - base);
    *len = (uint16_t)(end - start);
}

/* 处理结束关键字；成功返回 true */
static bool close_block(parse_ctx_t *ctx, uint8_t expect_a, uint8_t expect_b,
                        uint16_t line, const char *name)
{
    block_t *top = top_block(ctx);

    if (top == NULL || (top->type != expect_a && top->type != expect_b)) {
        char msg[48];

        snprintf(msg, sizeof(msg), "unmatched %s", name);
        parse_fail(ctx, line, msg);
        return false;
    }
    return true;
}

/* 解析一行；返回 -1 表示已失败 */
static void parse_line(parse_ctx_t *ctx, const char *line, uint16_t line_len,
                       uint16_t line_no)
{
    shell_tok_t toks[SHELL_TOKEN_MAX];
    int n = shell_lex(line, line_len, toks, SHELL_TOKEN_MAX);
    const char *base = ctx->script->text;
    uint32_t aoff = 0U;
    uint16_t alen = 0U;
    uint16_t idx;
    block_t *top;

    if (n <= 1) {
        return;                     /* 空行 / 纯注释 */
    }

    /* 第一 token 必须是标识符 */
    if (toks[0].type != SHELL_TOK_WORD) {
        parse_fail(ctx, line_no, "unknown statement");
        return;
    }

#define KW(k) shell_tok_ieq(&toks[0], (k))

    if (KW("JOB")) {
        idx = emit(ctx, SHELL_OP_NOP, line_no);
        if (idx == 0xFFFFU) {
            return;
        }
        push_block(ctx, BLK_JOB, idx, line_no);
        return;
    }
    if (KW("ENDJOB")) {
        if (!close_block(ctx, BLK_JOB, BLK_JOB, line_no, "ENDJOB")) {
            return;
        }
        ctx->depth--;
        (void)emit(ctx, SHELL_OP_ENDJOB, line_no);
        return;
    }
    if (KW("IF")) {
        arg_range(base, toks, (uint16_t)n, 1U, &aoff, &alen);
        idx = emit(ctx, SHELL_OP_IF_FALSE, line_no);
        if (idx == 0xFFFFU) {
            return;
        }
        ctx->script->code[idx].arg_off = aoff;
        ctx->script->code[idx].arg_len = alen;
        push_block(ctx, BLK_IF, idx, line_no);
        return;
    }
    if (KW("ELSE")) {
        uint16_t jump;

        top = top_block(ctx);
        if (top == NULL || top->type != BLK_IF) {
            parse_fail(ctx, line_no, "ELSE without IF");
            return;
        }
        jump = emit(ctx, SHELL_OP_JUMP, line_no);
        if (jump == 0xFFFFU) {
            return;
        }
        /* IF 条件为假时跳到 ELSE 之后 */
        ctx->script->code[top->instr].target = (uint16_t)(jump + 1U);
        top->extra = jump;          /* ELSE 的 JUMP 在 END 处补目标 */
        return;
    }
    if (KW("END")) {
        top = top_block(ctx);
        if (!close_block(ctx, BLK_IF, BLK_IF, line_no, "END")) {
            return;
        }
        if (top->extra != 0U) {
            ctx->script->code[top->extra].target = ctx->count;
        } else {
            ctx->script->code[top->instr].target = ctx->count;
        }
        ctx->depth--;
        return;
    }
    if (KW("FOR") || KW("LOOP")) {
        bool is_for = KW("FOR");
        uint16_t next;

        arg_range(base, toks, (uint16_t)n, 1U, &aoff, &alen);
        idx = emit(ctx, SHELL_OP_FOR_INIT, line_no);
        if (idx == 0xFFFFU) {
            return;
        }
        ctx->script->code[idx].arg_off = aoff;
        ctx->script->code[idx].arg_len = alen;
        if (!push_block(ctx, is_for ? BLK_FOR : BLK_LOOP, idx, line_no)) {
            return;
        }
        (void)next;                 /* FOR_NEXT 在 ENDFOR 处生成 */
        return;
    }
    if (KW("ENDFOR") || KW("ENDLOOP")) {
        bool is_for = KW("ENDFOR");
        uint16_t next;

        top = top_block(ctx);
        if (top == NULL ||
            (is_for ? top->type != BLK_FOR : top->type != BLK_LOOP)) {
            parse_fail(ctx, line_no, is_for ? "unmatched ENDFOR"
                                            : "unmatched ENDLOOP");
            return;
        }
        next = emit(ctx, SHELL_OP_FOR_NEXT, line_no);
        if (next == 0xFFFFU) {
            return;
        }
        ctx->script->code[next].arg_off = ctx->script->code[top->instr].arg_off;
        ctx->script->code[next].arg_len = ctx->script->code[top->instr].arg_len;
        ctx->script->code[next].target = (uint16_t)(top->instr + 1U);
        ctx->script->code[top->instr].target = next;   /* 首次条件不满足时跳出 */
        top->extra = next;          /* BREAK 的目标 */
        pend_patch(ctx, ctx->depth, (uint16_t)(next + 1U), next);
        ctx->depth--;
        return;
    }
    if (KW("WHILE")) {
        arg_range(base, toks, (uint16_t)n, 1U, &aoff, &alen);
        idx = emit(ctx, SHELL_OP_WHILE_CHECK, line_no);
        if (idx == 0xFFFFU) {
            return;
        }
        ctx->script->code[idx].arg_off = aoff;
        ctx->script->code[idx].arg_len = alen;
        push_block(ctx, BLK_WHILE, idx, line_no);
        return;
    }
    if (KW("ENDWHILE")) {
        if (!close_block(ctx, BLK_WHILE, BLK_WHILE, line_no, "ENDWHILE")) {
            return;
        }
        top = top_block(ctx);
        idx = emit(ctx, SHELL_OP_JUMP, line_no);
        if (idx == 0xFFFFU) {
            return;
        }
        ctx->script->code[idx].target = top->instr;
        ctx->script->code[top->instr].target = (uint16_t)(idx + 1U);
        pend_patch(ctx, ctx->depth, (uint16_t)(idx + 1U), top->instr);
        ctx->depth--;
        return;
    }
    if (KW("BREAK") || KW("CONTINUE")) {
        bool is_break = KW("BREAK");
        block_t *loop;

        loop = find_loop(ctx, NULL);
        if (loop == NULL) {
            parse_fail(ctx, line_no, is_break ? "BREAK outside loop"
                                              : "CONTINUE outside loop");
            return;
        }
        idx = emit(ctx, is_break ? SHELL_OP_BREAK : SHELL_OP_CONTINUE, line_no);
        if (idx == 0xFFFFU) {
            return;
        }
        ctx->script->code[idx].target = 0xFFFFU;   /* 循环闭合时回填 */
        pend_add(ctx, idx);
        return;
    }
    if (KW("ON")) {
        uint8_t type = 0U;

        if (n >= 2 && shell_tok_ieq(&toks[1], "ERROR")) {
            type = BLK_ERR;
        } else if (n >= 2 && shell_tok_ieq(&toks[1], "STOP")) {
            type = BLK_STOP;
        } else {
            parse_fail(ctx, line_no, "ON expects ERROR or STOP");
            return;
        }
        idx = emit(ctx, SHELL_OP_JUMP, line_no);   /* 正常流程跳过钩子 */
        if (idx == 0xFFFFU) {
            return;
        }
        push_block(ctx, type, idx, line_no);
        if (type == BLK_ERR) {
            ctx->script->err_start = (uint16_t)(idx + 1U);
        } else {
            ctx->script->stop_start = (uint16_t)(idx + 1U);
        }
        return;
    }
    if (KW("ENDERROR") || KW("ENDSTOP")) {
        bool is_err = KW("ENDERROR");

        top = top_block(ctx);
        if (top == NULL || (is_err ? top->type != BLK_ERR : top->type != BLK_STOP)) {
            parse_fail(ctx, line_no, is_err ? "unmatched ENDERROR"
                                            : "unmatched ENDSTOP");
            return;
        }
        ctx->script->code[top->instr].target = ctx->count;
        if (is_err) {
            ctx->script->err_end = ctx->count;
        } else {
            ctx->script->stop_end = ctx->count;
        }
        ctx->depth--;
        return;
    }
    if (KW("SET")) {
        arg_range(base, toks, (uint16_t)n, 1U, &aoff, &alen);
        idx = emit(ctx, SHELL_OP_SET, line_no);
        if (idx != 0xFFFFU) {
            ctx->script->code[idx].arg_off = aoff;
            ctx->script->code[idx].arg_len = alen;
        }
        return;
    }
    if (KW("PRINT")) {
        arg_range(base, toks, (uint16_t)n, 1U, &aoff, &alen);
        idx = emit(ctx, SHELL_OP_PRINT, line_no);
        if (idx != 0xFFFFU) {
            ctx->script->code[idx].arg_off = aoff;
            ctx->script->code[idx].arg_len = alen;
        }
        return;
    }
    if (KW("SHELL")) {
        uint8_t op = SHELL_OP_SHELL_CMD;

        if (n >= 2 && shell_tok_ieq(&toks[1], "PRINT")) {
            op = SHELL_OP_SHELL_PRINT;
        } else if (n >= 2 && shell_tok_ieq(&toks[1], "LOG")) {
            op = SHELL_OP_SHELL_LOG;
        } else if (n >= 2 && shell_tok_ieq(&toks[1], "STATE")) {
            op = SHELL_OP_SHELL_STATE;
        }
        arg_range(base, toks, (uint16_t)n, 2U, &aoff, &alen);
        idx = emit(ctx, op, line_no);
        if (idx != 0xFFFFU) {
            ctx->script->code[idx].arg_off = aoff;
            ctx->script->code[idx].arg_len = alen;
            if (op == SHELL_OP_SHELL_CMD) {
                /* 透传命令名 + 参数：从 SHELL 之后开始 */
                arg_range(base, toks, (uint16_t)n, 1U, &aoff, &alen);
                ctx->script->code[idx].arg_off = aoff;
                ctx->script->code[idx].arg_len = alen;
            }
        }
        return;
    }
    if (KW("WAIT")) {
        if (n >= 2 && shell_tok_ieq(&toks[1], "TIME")) {
            arg_range(base, toks, (uint16_t)n, 2U, &aoff, &alen);
            idx = emit(ctx, SHELL_OP_WAIT_TIME, line_no);
        } else {
            parse_fail(ctx, line_no, "WAIT supports TIME in this build");
            return;
        }
        if (idx != 0xFFFFU) {
            ctx->script->code[idx].arg_off = aoff;
            ctx->script->code[idx].arg_len = alen;
        }
        return;
    }
    if (KW("START") || KW("STOP")) {
        bool start = KW("START");

        if (n < 3 || !shell_tok_ieq(&toks[1], "JOB") ||
            toks[2].type == SHELL_TOK_EOF) {
            parse_fail(ctx, line_no, start ? "START requires JOB keyword"
                                           : "STOP requires JOB keyword");
            return;
        }
        idx = emit(ctx, start ? SHELL_OP_START_JOB : SHELL_OP_STOP_JOB, line_no);
        if (idx != 0xFFFFU) {
            ctx->script->code[idx].arg_off =
                (uint32_t)(toks[2].p - base);
            ctx->script->code[idx].arg_len = toks[2].len;
        }
        return;
    }
    if (KW("LABEL")) {
        if (n < 2 || toks[1].type != SHELL_TOK_WORD) {
            parse_fail(ctx, line_no, "LABEL requires a name");
            return;
        }
        idx = emit(ctx, SHELL_OP_NOP, line_no);
        if (idx != 0xFFFFU) {
            add_label(ctx, toks[1].p, toks[1].len, idx, line_no);
        }
        return;
    }
    if (KW("GOTO") || KW("CALL")) {
        if (n < 2 || toks[1].type != SHELL_TOK_WORD) {
            parse_fail(ctx, line_no, "GOTO/CALL requires a label");
            return;
        }
        idx = emit(ctx, KW("GOTO") ? SHELL_OP_GOTO : SHELL_OP_CALL, line_no);
        if (idx != 0xFFFFU) {
            ctx->script->code[idx].arg_off =
                (uint32_t)(toks[1].p - base);
            ctx->script->code[idx].arg_len = toks[1].len;
            ctx->script->code[idx].target = 0xFFFFU;   /* 解析结束后回填 */
        }
        return;
    }
    if (KW("RETURN")) {
        (void)emit(ctx, SHELL_OP_RETURN, line_no);
        return;
    }

    parse_fail(ctx, line_no, "unknown statement");
#undef KW
}

/* 回填 GOTO/CALL 目标（BREAK/CONTINUE 已在块闭合时回填） */
static void patch_targets(parse_ctx_t *ctx)
{
    uint16_t i;

    for (i = 0U; i < ctx->count; i++) {
        shell_instr_t *ins = &ctx->script->code[i];

        if (ins->op == SHELL_OP_GOTO || ins->op == SHELL_OP_CALL) {
            uint16_t target;

            if (!find_label(ctx, &ctx->script->text[ins->arg_off],
                            ins->arg_len, &target)) {
                parse_fail(ctx, ins->line, "undefined label");
                return;
            }
            ins->target = target;
        }
    }
}

int shell_script_parse(shell_script_t *script, const char *text,
                       shell_err_t *err)
{
    parse_ctx_t ctx;
    uint32_t i = 0U;
    uint32_t len;
    uint16_t line_no = 0U;

    if (script == NULL || text == NULL) {
        return -1;
    }
    memset(script, 0, sizeof(*script));
    if (err != NULL) {
        memset(err, 0, sizeof(*err));
    }

    len = (uint32_t)strlen(text);
    script->text = (char *)ros_mem_malloc(len + 1U);
    if (script->text == NULL) {
        if (err != NULL) {
            err->code = SHELL_ERR_RESOURCE;
            snprintf(err->msg, sizeof(err->msg), "out of memory");
        }
        return -1;
    }
    memcpy(script->text, text, len + 1U);
    script->text_len = len;
    script->code = (shell_instr_t *)ros_mem_calloc(SHELL_INSTR_MAX,
                                                   sizeof(shell_instr_t));
    if (script->code == NULL) {
        ros_mem_free(script->text);
        script->text = NULL;
        if (err != NULL) {
            err->code = SHELL_ERR_RESOURCE;
            snprintf(err->msg, sizeof(err->msg), "out of memory");
        }
        return -1;
    }

    memset(&ctx, 0, sizeof(ctx));
    ctx.script = script;
    ctx.err = err;

    /* 逐行解析（行号从 1 开始） */
    while (i <= len) {
        uint32_t start = i;

        while (i < len && script->text[i] != '\n') {
            i++;
        }
        line_no++;
        parse_line(&ctx, &script->text[start], (uint16_t)(i - start), line_no);
        if (ctx.failed) {
            break;
        }
        if (i < len) {
            i++;    /* 跳过 \n */
        } else {
            break;
        }
    }

    if (!ctx.failed && ctx.depth != 0U) {
        parse_fail(&ctx, line_no, "unclosed block");
    }
    if (!ctx.failed) {
        patch_targets(&ctx);
    }

    if (ctx.failed) {
        shell_script_free(script);
        return -1;
    }
    script->count = ctx.count;
    return 0;
}

void shell_script_free(shell_script_t *script)
{
    if (script == NULL) {
        return;
    }
    if (script->code != NULL) {
        ros_mem_free(script->code);
    }
    if (script->text != NULL) {
        ros_mem_free(script->text);
    }
    memset(script, 0, sizeof(*script));
}
