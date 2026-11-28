#ifndef FS_CMD_H
#define FS_CMD_H

/*
 * fs_cmd.h - 通用文件系统 Shell 命令
 * ============================================================================
 *
 * 注册与具体文件系统无关的命令，全部通过 VFS 访问：
 *   ls / cat / mkdir / rmdir / rm / stat / df / put / run
 *
 * run <path> 为脚本创建一个动态任务（TCB 与栈均来自内核堆），在任务里用
 * roshell 流式执行（rs_exec_file：行索引 + 按行取源，不整读文件），
 * 命令本身立即返回。并发数量不设上限，只受剩余堆大小约束；可执行规模由
 * RS_LINE_SRC_MAX（行数）与 RS_LINE_MAX（单行长度）决定，与文件字节数无关。
 *
 * 应用挂载好文件系统后调用 fs_cmd_register() 即可。
 */

#ifdef __cplusplus
extern "C" {
#endif

/* 注册全部 FS 命令；返回 0 成功，-1 表示有命令注册失败 */
int fs_cmd_register(void);

#ifdef __cplusplus
}
#endif

#endif /* FS_CMD_H */
