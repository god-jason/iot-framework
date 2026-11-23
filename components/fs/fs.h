#ifndef ROS_FS_H
#define ROS_FS_H

/*
 * fs - littlefs 文件系统封装
 *
 * 只做 littlefs 之上的轻量封装，不绑定具体存储介质：
 * 块设备（读/写/擦除）由调用者通过 lfs_config 提供。
 *
 * 约定：
 *   - 所有接口返回 0 表示成功，负值为 littlefs 错误码（LFS_ERR_*）；
 *   - fs_read/fs_write 返回读写的字节数，负值为错误码；
 *   - fs_file_t / fs_dir_t 由调用者静态分配，句柄不能跨任务并发使用。
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "lfs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 文件缓存大小：lfs_config.cache_size 不能超过该值 */
#define FS_FILE_CACHE_SIZE      256U

/* 打开标志，与 littlefs 数值对齐，可按位或组合 */
typedef enum {
    FS_O_RDONLY = LFS_O_RDONLY,
    FS_O_WRONLY = LFS_O_WRONLY,
    FS_O_RDWR   = LFS_O_RDWR,
    FS_O_CREAT  = LFS_O_CREAT,
    FS_O_EXCL   = LFS_O_EXCL,
    FS_O_TRUNC  = LFS_O_TRUNC,
    FS_O_APPEND = LFS_O_APPEND
} fs_open_flags_e;

/* 文件类型 */
typedef enum {
    FS_TYPE_FILE = LFS_TYPE_REG,
    FS_TYPE_DIR  = LFS_TYPE_DIR
} fs_type_e;

/* 文件句柄（内含 littlefs 文件对象与静态缓存） */
typedef struct {
    lfs_file_t file;
    struct lfs_file_config cfg;
    uint8_t cache[FS_FILE_CACHE_SIZE];
} fs_file_t;

/* 目录句柄 */
typedef struct {
    lfs_dir_t dir;
} fs_dir_t;

/* 文件/目录信息 */
typedef struct {
    uint32_t size;                 /* 文件大小（字节），目录为 0 */
    fs_type_e type;                /* 文件或目录 */
    char name[LFS_NAME_MAX + 1];   /* 名称 */
} fs_info_t;

/* 文件系统配置 */
typedef struct {
    const struct lfs_config *lfs_cfg; /* 块设备配置，必需 */
    bool auto_format;                 /* 挂载失败时自动格式化 */
} fs_config_t;

/* 初始化：挂载文件系统（可选自动格式化），成功后可重复调用 */
int fs_init(const fs_config_t *cfg);

/* 卸载并释放内部状态；未挂载时返回 0 */
int fs_deinit(void);

/* 挂载（需先 fs_init 保存过配置） */
int fs_mount(void);

/* 卸载 */
int fs_unmount(void);

/* 格式化后重新挂载（会清空文件系统） */
int fs_format(void);

/* 是否已挂载 */
bool fs_is_mounted(void);

/* ------------------------------ 文件操作 ------------------------------ */

/* 打开文件；flags 为 fs_open_flags_e 组合 */
int fs_open(fs_file_t *file, const char *path, int flags);

/* 关闭文件，返回 0 或错误码 */
int fs_close(fs_file_t *file);

/* 读取，返回字节数或负错误码 */
int fs_read(fs_file_t *file, void *buf, size_t len);

/* 写入，返回字节数或负错误码 */
int fs_write(fs_file_t *file, const void *buf, size_t len);

/* 定位，whence 取 LFS_SEEK_SET/CUR/END，返回新位置或负错误码 */
int fs_seek(fs_file_t *file, int32_t offset, int whence);

/* 当前位置，负值为错误码 */
int32_t fs_tell(fs_file_t *file);

/* 文件大小，负值为错误码 */
int32_t fs_size(fs_file_t *file);

/* 同步到存储 */
int fs_sync(fs_file_t *file);

/* ------------------------------ 路径操作 ------------------------------ */

int fs_remove(const char *path);
int fs_rename(const char *oldpath, const char *newpath);
int fs_mkdir(const char *path);

/* 查询文件/目录信息 */
int fs_stat(const char *path, fs_info_t *info);

/* ------------------------------ 目录遍历 ------------------------------ */

int fs_dir_open(fs_dir_t *dir, const char *path);

/* 读取下一个目录项：1 = 读到一项，0 = 遍历结束，负值 = 错误码 */
int fs_dir_read(fs_dir_t *dir, fs_info_t *info);

int fs_dir_close(fs_dir_t *dir);

#ifdef __cplusplus
}
#endif

#endif /* ROS_FS_H */
