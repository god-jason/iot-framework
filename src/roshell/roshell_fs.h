#ifndef RS_FS_H
#define RS_FS_H

/*
 * roshell_fs.h - 直接执行文件系统中的脚本（流式，不整读）
 *
 * 与 rs_exec() 的区别：
 *   rs_exec()      要求整段脚本在 RAM 里（调用方先读全，再一次性扫描执行）；
 *   rs_exec_file() 通过 VFS 打开文件后边读边执行：先扫一遍建行索引，
 *                  再逐行校验、逐行取行执行，脚本正文始终不驻留内存。
 *
 * 内存代价：行偏移表 4B/行 + 块配对表 2B/行 + 行分类表 1B/行，
 * 行数上限 RS_LINE_SRC_MAX（默认 256），与文件字节数无关。
 *
 * 注意：执行期间文件保持打开且内容不变（脚本里改写同一文件会导致行号错乱）。
 */

#include "roshell.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 扫描并同步执行 path 指向的脚本文件。
 * 返回 RS_OK 或错误码（RS_ERR_NOT_FOUND / RS_ERR_LIMIT / RS_ERR_PARSE …），
 * err 可为 NULL 且 err->line 为出错行号（1 起）。
 */
int rs_exec_file(roshell_t *rs, const char *path, rs_err_t *err);

/*
 * 执行前的文件校验：存在、不是目录、非空；成功时可通过 size 取字节数。
 * 供 run 这类命令在投递前同步报错（避免把明显错误丢到后台任务里）。
 * 返回 RS_OK / RS_ERR_NOT_FOUND / RS_ERR_PARAM，err->msg 为可读原因。
 */
int rs_file_check(const char *path, uint32_t *size, rs_err_t *err);

/*
 * 更底层：直接给按行取源回调与已知行数（不经过文件系统）。
 * 供 RAM 缓冲、自定义存储等场景使用；回调契约见 roshell.h 的 rs_line_src_t。
 */
int rs_exec_lines(roshell_t *rs, rs_line_src_t src, void *ctx, uint16_t lines,
                  rs_err_t *err);

#ifdef __cplusplus
}
#endif

#endif /* RS_FS_H */
