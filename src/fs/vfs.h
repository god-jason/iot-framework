#ifndef VFS_H
#define VFS_H

/*
 * vfs.h - 虚拟文件系统：挂载表与路径前缀分发
 * ============================================================================
 *
 * 设计目标：
 *   - 应用只依赖 vfs_* 接口，不直接调用具体文件系统；
 *   - 支持多个挂载点（例如 "/"、"ram"、"/logs"），按最长前缀匹配；
 *   - 文件/目录句柄由调用者静态分配，VFS 不做动态内存分配。
 *
 * 路径规则：
 *   - 挂载点必须以 '/' 开头，根挂载点为 "/"；
 *   - 访问路径在挂载点之后的部分（去掉挂载前缀）传给后端；
 *   - 例如挂载点 "/logs"，访问 "/logs/a.txt" 时后端收到 "/a.txt"。
 *
 * 句柄使用约定：
 *   - vfs_file_t / vfs_dir_t 由调用者分配，使用前必须 memset 为 0；
 *   - vfs_format 要求该挂载点没有打开的文件/目录句柄。
 */

#include "fs.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef VFS_MAX_MOUNTS
#define VFS_MAX_MOUNTS      8
#endif

#ifndef VFS_MOUNT_MAX
#define VFS_MOUNT_MAX       32
#endif

struct vfs_mount;

/* 文件句柄：由调用者分配，open 成功后 used=1 */
typedef struct {
    struct vfs_mount *mnt;
    void *fp;
    uint32_t flags;
    uint8_t used;
} vfs_file_t;

/* 目录句柄：由调用者分配 */
typedef struct {
    struct vfs_mount *mnt;
    void *dp;
    uint8_t used;
} vfs_dir_t;

/* 挂载/卸载/格式化；mp 为挂载点（"/" 或 "/xxx"） */
int vfs_mount(const char *mp, const fs_ops_t *ops, void *ctx);
int vfs_umount(const char *mp);
int vfs_format(const char *mp);

/* 空间统计；path 可为挂载点或其下任意路径 */
int vfs_statfs(const char *path, fs_statfs_t *stat);

/* 文件操作 */
int vfs_open(const char *path, uint32_t flags, vfs_file_t *file);
int vfs_close(vfs_file_t *file);
int32_t vfs_read(vfs_file_t *file, void *buf, uint32_t size);
int32_t vfs_write(vfs_file_t *file, const void *buf, uint32_t size);
int vfs_seek(vfs_file_t *file, int32_t offset, int whence);
int32_t vfs_tell(vfs_file_t *file);
uint32_t vfs_size(vfs_file_t *file);

/* 命名空间操作 */
int vfs_stat(const char *path, fs_stat_t *stat);
int vfs_exists(const char *path);       /* 1=存在，0=不存在，负值=错误 */
int vfs_isdir(const char *path);        /* 1=目录，0=非目录，负值=错误 */
int vfs_mkdir(const char *path);
int vfs_rmdir(const char *path);
int vfs_unlink(const char *path);
int vfs_rename(const char *oldpath, const char *newpath);

/* 目录遍历：0=有项，1=结束，负值=错误 */
int vfs_opendir(const char *path, vfs_dir_t *dir);
int vfs_readdir(vfs_dir_t *dir, fs_dirent_t *entry);
int vfs_closedir(vfs_dir_t *dir);

#ifdef __cplusplus
}
#endif

#endif /* VFS_H */
