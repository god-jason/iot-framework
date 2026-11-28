#ifndef FS_H
#define FS_H

/*
 * fs.h - 通用文件系统接口（FSAL）
 * ============================================================================
 *
 * 本文件定义与具体文件系统无关的：
 *   - 错误码、打开标志、Seek 基准；
 *   - 文件/目录/空间统计数据结构；
 *   - 文件系统后端操作表 fs_ops_t。
 *
 * 后端（当前为 littlefs）实现 fs_ops_t，由 VFS 挂载到某个路径前缀。
 * 上层应用统一通过 vfs.h 访问，不直接依赖具体文件系统。
 */

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 单个文件名最大长度（不含 '\0'），需与后端 name_max 一致 */
#ifndef FS_NAME_MAX
#define FS_NAME_MAX      63
#endif

/* 完整路径最大长度，VFS 内部按此分配栈缓冲 */
#ifndef FS_PATH_MAX
#define FS_PATH_MAX      127
#endif

/* 打开标志：数值与 littlefs 保持一致，后端可直接透传 */
#define FS_O_RDONLY      0x0001   /* 只读 */
#define FS_O_WRONLY      0x0002   /* 只写 */
#define FS_O_RDWR        0x0003   /* 读写 */
#define FS_O_CREAT       0x0100   /* 不存在则创建 */
#define FS_O_EXCL        0x0200   /* 与 CREAT 同用，已存在则失败 */
#define FS_O_TRUNC       0x0400   /* 打开时清空 */
#define FS_O_APPEND      0x0800   /* 每次写前定位到文件尾 */

/* Seek 基准：数值与 littlefs 保持一致 */
#define FS_SEEK_SET      0
#define FS_SEEK_CUR      1
#define FS_SEEK_END      2

/* 文件类型 */
#define FS_TYPE_FILE     0x01
#define FS_TYPE_DIR      0x02

/*
 * 错误码。
 * 常见错误与 littlefs 数值一致，便于后端直接透传；
 * FS_ERR_FORMAT/STATE/BUSY 为本层补充。
 */
typedef enum {
    FS_OK               = 0,

    FS_ERR_IO           = -5,
    FS_ERR_NOENT        = -2,
    FS_ERR_EXIST        = -17,
    FS_ERR_NOTDIR       = -20,
    FS_ERR_ISDIR        = -21,
    FS_ERR_NOTEMPTY     = -39,
    FS_ERR_BADF         = -9,
    FS_ERR_FBIG         = -27,
    FS_ERR_INVAL        = -22,
    FS_ERR_NOSPC        = -28,
    FS_ERR_NOMEM        = -12,
    FS_ERR_NOATTR       = -61,
    FS_ERR_NAMETOOLONG  = -36,
    FS_ERR_CORRUPT      = -84,

    FS_ERR_NOTSUP       = -100,
    FS_ERR_FORMAT       = -101,
    FS_ERR_STATE        = -102,
    FS_ERR_BUSY         = -103
} fs_error_t;

/*
 * 块设备接口。
 * 所有地址均为分区的绝对字节地址；erase 的 addr/size 必须按擦除块对齐。
 */
typedef struct {
    int (*read)(void *ctx, uint32_t addr, void *buf, uint32_t size);
    int (*program)(void *ctx, uint32_t addr, const void *buf, uint32_t size);
    int (*erase)(void *ctx, uint32_t addr, uint32_t size);
    int (*sync)(void *ctx);
    void *ctx;
} fs_blkdev_t;

/* 空间统计；used_blocks 由后端按已分配块估算 */
typedef struct {
    uint32_t total_bytes;
    uint32_t used_bytes;
    uint32_t free_bytes;
    uint32_t total_blocks;
    uint32_t used_blocks;
    uint32_t block_size;
} fs_statfs_t;

/* 文件/目录属性 */
typedef struct {
    uint8_t type;                 /* FS_TYPE_FILE / FS_TYPE_DIR */
    uint32_t size;                /* 目录为 0 */
    char name[FS_NAME_MAX + 1];   /* '\0' 结尾 */
} fs_stat_t;

/* 目录项（readdir 返回） */
typedef struct {
    uint8_t type;
    uint32_t size;
    char name[FS_NAME_MAX + 1];
} fs_dirent_t;

/*
 * 文件系统后端操作表。
 * ctx   ：后端实例指针；
 * fp/dp ：后端返回的句柄，VFS 原样保存，不可解释。
 *
 * open 成功时后端把句柄写入 *fp；readdir 返回 0=有项、1=结束、负值=错误。
 */
typedef struct fs_ops {
    int (*format)(void *ctx);
    int (*mount)(void *ctx);
    int (*unmount)(void *ctx);
    int (*statfs)(void *ctx, fs_statfs_t *stat);

    int (*open)(void *ctx, const char *path, uint32_t flags, void **fp);
    int (*close)(void *ctx, void *fp);
    int32_t (*read)(void *ctx, void *fp, void *buf, uint32_t size);
    int32_t (*write)(void *ctx, void *fp, const void *buf, uint32_t size);
    int (*seek)(void *ctx, void *fp, int32_t offset, int whence);
    int32_t (*tell)(void *ctx, void *fp);
    uint32_t (*size)(void *ctx, void *fp);

    int (*stat)(void *ctx, const char *path, fs_stat_t *stat);
    int (*mkdir)(void *ctx, const char *path);
    int (*rmdir)(void *ctx, const char *path);
    int (*unlink)(void *ctx, const char *path);
    int (*rename)(void *ctx, const char *oldpath, const char *newpath);

    int (*opendir)(void *ctx, const char *path, void **dp);
    int (*readdir)(void *ctx, void *dp, fs_dirent_t *entry);
    int (*closedir)(void *ctx, void *dp);
} fs_ops_t;

/* 错误码转可读字符串 */
const char *fs_strerror(int error);

#ifdef __cplusplus
}
#endif

#endif /* FS_H */
