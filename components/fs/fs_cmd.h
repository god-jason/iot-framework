#ifndef FS_CMD_H
#define FS_CMD_H

/*
 * fs_cmd.h - 通用文件系统 Shell 命令
 * ============================================================================
 *
 * 注册与具体文件系统无关的命令，全部通过 VFS 访问：
 *   ls / cat / mkdir / rmdir / rm / stat / df / put
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
