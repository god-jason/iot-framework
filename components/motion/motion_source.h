#ifndef MOTION_SOURCE_H
#define MOTION_SOURCE_H

/*
 * motion_source.h —— MotionScript 脚本数据源抽象。
 *
 * 职责：用一组函数指针屏蔽"脚本从哪里来"的差异（RAM 字符串、文件、
 *       Flash、网络流等），让加载器只依赖 read_line / seek / tell。
 *
 * 数据流：motion_source_string_init() 填充结构体并挂上内建实现；
 *         加载器反复调用 read_line() 取行，需要回退或跳转时用
 *         tell() 记录偏移、seek() 定位。
 *
 * 边界约束：read_line() 的缓冲区由调用方提供，实现不得越界写入；
 *           行超长时由实现负责丢弃该行剩余内容并返回 -2。
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 数据源句柄的前向声明，便于函数指针自引用 */
typedef struct motion_source motion_source_t;

struct motion_source {
    void *ctx; /* 具体实现的私有上下文，可为 NULL；字符串源中指向自身 */

    /* 打开数据源。
     * 参数：source 当前句柄；name 资源名（字符串源忽略）。
     * 返回：0 成功，其他值失败。 */
    int (*open)(motion_source_t *source, const char *name);

    /* 读取一行（不含行尾换行符，结果以 '\0' 结尾）。
     * 参数：buffer 输出缓冲；size 缓冲字节数。
     * 返回：1 读到一行，0 文件结束，-1 读错误，-2 行超长。 */
    int (*read_line)(motion_source_t *source, char *buffer, uint16_t size);

    /* 将读取位置定位到 offset 字节处；返回 0 成功，其他失败 */
    int (*seek)(motion_source_t *source, uint32_t offset);
    /* 取出当前读取位置；返回 0 成功，其他失败 */
    int (*tell)(motion_source_t *source, uint32_t *offset);
    /* 关闭数据源并释放内部资源（静态实现通常为空操作）；返回 0 成功 */
    int (*close)(motion_source_t *source);

    /* RAM 字符串源内建上下文，供字符串源实现使用 */
    const char *text; /* 脚本文本起始地址，以 '\0' 结尾 */
    uint32_t length;  /* 文本字节长度（不含结尾 '\0'） */
    uint32_t pos;     /* 当前读取位置，范围 0 ~ length */
};

/* 用一段以 '\0' 结尾的内存文本初始化数据源。
 * 参数：source 待初始化的句柄；text 脚本文本，可为 NULL（此时长度记为 0）。
 * 说明：会先清零整个结构体，再挂接内建函数表；text 不被复制，
 *       调用方必须保证其生命周期覆盖整个使用期间。 */
void motion_source_string_init(motion_source_t *source, const char *text);

#ifdef __cplusplus
}
#endif

#endif /* MOTION_SOURCE_H */
