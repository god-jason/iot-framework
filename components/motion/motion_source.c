#include "motion_source.h"

/*
 * motion_source.c —— 脚本数据源的 RAM 字符串实现。
 *
 * 模块职责：把一段常驻内存的 '\0' 结尾文本包装成 motion_source_t，
 *           提供按行读取与随机定位能力，供解析/加载流程使用。
 *
 * 数据流：text + pos 即读取游标；read_line 从 pos 处扫描到行尾，
 *         把内容复制到调用方缓冲区；seek/tell 直接读写 pos。
 *
 * 边界约束：不复制 text，也不做内存分配；pos 始终满足 0 <= pos <= length。
 */

#include <string.h>

/* 打开字符串源：仅校验上下文并复位读取位置。
 * 参数：source 数据源；name 未使用（保留接口一致性）。
 * 返回：0 成功；-1 source 或 text 为空。 */
static int string_open(motion_source_t *source, const char *name)
{
    (void)name;

    if (source == NULL || source->text == NULL) {
        return -1;
    }

    source->pos = 0U;
    return 0;
}

/* 从字符串源读取一行。
 * 参数：source 数据源；buffer 输出缓冲；size 缓冲容量（字节）。
 * 返回：1 读到一行，0 已到末尾，-1 参数错误，-2 行超长。
 * 说明：自动去掉 '\r'，以 '\n' 作为行结束；首行会跳过 UTF-8 BOM；
 *       行超长时吞掉本行剩余内容后返回 -2，保证下次从新行开始。 */
static int string_read_line(motion_source_t *source, char *buffer,
                            uint16_t size)
{
    uint32_t i = 0U; /* 已写入 buffer 的字符数 */

    if (source == NULL || buffer == NULL || size == 0U) {
        return -1;
    }

    if (source->pos >= source->length) {
        buffer[0] = '\0';
        return 0;
    }

    /* 跳过 UTF-8 BOM（仅文件开头可能出现，避免污染首条指令） */
    if (source->pos == 0U && source->length >= 3U &&
        (unsigned char)source->text[0] == 0xEFU &&
        (unsigned char)source->text[1] == 0xBBU &&
        (unsigned char)source->text[2] == 0xBFU) {
        source->pos = 3U;
    }

    while (source->pos < source->length) {
        char c = source->text[source->pos++];

        /* 忽略 CR，兼容 CRLF 与 CR 换行 */
        if (c == '\r') {
            continue;
        }

        /* 遇到 LF 即完成一行 */
        if (c == '\n') {
            buffer[i] = '\0';
            return 1;
        }

        /* 预留 1 字节存放结尾 '\0'，因此实际可写 size - 1 个字符 */
        if ((uint32_t)(i + 1U) < (uint32_t)size) {
            buffer[i++] = c;
        } else {
            /* 行超长：丢弃本行剩余内容并报告错误 */
            while (source->pos < source->length &&
                   source->text[source->pos] != '\n') {
                source->pos++;
            }
            if (source->pos < source->length) {
                source->pos++; /* 跳过换行符，使下一行从正确位置开始 */
            }
            buffer[size - 1U] = '\0'; /* 保证输出仍以 '\0' 结尾 */
            return -2;
        }
    }

    buffer[i] = '\0';
    return (i > 0U) ? 1 : 0; /* 末尾无换行：有内容算一行，否则视为结束 */
}

/* 定位到指定字节偏移。
 * 参数：source 数据源；offset 目标偏移，允许等于 length（表示末尾）。
 * 返回：0 成功；-1 参数非法或 offset 越界。 */
static int string_seek(motion_source_t *source, uint32_t offset)
{
    if (source == NULL || offset > source->length) {
        return -1;
    }

    source->pos = offset;
    return 0;
}

/* 取当前读取偏移。
 * 参数：source 数据源；offset 输出当前偏移。
 * 返回：0 成功；-1 参数为空。 */
static int string_tell(motion_source_t *source, uint32_t *offset)
{
    if (source == NULL || offset == NULL) {
        return -1;
    }

    *offset = source->pos;
    return 0;
}

/* 关闭字符串源：文本归调用方所有，无需释放，仅返回成功。 */
static int string_close(motion_source_t *source)
{
    (void)source;
    return 0;
}

/* 初始化 RAM 字符串数据源。
 * 参数：source 待初始化句柄；text 以 '\0' 结尾的脚本文本，可为 NULL。
 * 返回：无。source 为 NULL 时静默返回；text 不被复制。 */
void motion_source_string_init(motion_source_t *source, const char *text)
{
    if (source == NULL) {
        return;
    }

    memset(source, 0, sizeof(*source));

    source->ctx = source; /* 字符串源的上下文即自身 */
    source->text = text;
    source->length = (text != NULL) ? (uint32_t)strlen(text) : 0U;
    source->pos = 0U;

    /* 挂接内建实现 */
    source->open = string_open;
    source->read_line = string_read_line;
    source->seek = string_seek;
    source->tell = string_tell;
    source->close = string_close;
}
