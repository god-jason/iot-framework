#ifndef FS_CMD_H
#define FS_CMD_H

/*
 * fs_cmd.h - 通用文件系统 Shell 命令
 * ============================================================================
 *
 * 注册与具体文件系统无关的命令，全部通过 VFS 访问：
 *   ls / cat / mkdir / rmdir / rm / stat / df / put / run
 *
 * run <path> 会把脚本文件加载到内核堆，为它创建一个动态任务（TCB 与栈
 * 均来自内核堆）用 roshell 解释，命令本身立即返回；并发数量不设上限，
 * 只受剩余堆大小约束。单个脚本上限 FS_RUN_SCRIPT_MAX（默认 RS_SCRIPT_MAX）。
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
