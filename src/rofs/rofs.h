#ifndef ROFS_H
#define ROFS_H

/*
 * rofs.h - ROFS 1.0 只读文件系统
 * ============================================================================
 *
 * ROFS 面向 MCU 静态资源：镜像由 PC 端 tools/make_rofs.py 生成，
 * MCU 端只负责挂载、查找、读取与目录遍历，不支持写操作。
 *
 * 镜像结构（紧凑排列，无对齐填充）：
 *
 *   +---------------------------+
 *   | Super Header (32B)        |
 *   +---------------------------+
 *   | Record 1                  |
 *   |   Header(16B)+Name+Body   |
 *   +---------------------------+
 *   | Record 2 ...              |
 *   +---------------------------+
 *   | END! (4B)                 |
 *   +---------------------------+
 *
 * 特点：
 *   - 不使用 malloc，文件/目录句柄由调用者静态分配；
 *   - 顺序扫描定位文件，读取支持随机 seek；
 *   - 每次访问都做边界检查，不信任镜像中的长度字段；
 *   - 记录按完整相对路径组织（如 "config/device.json"），不建目录树。
 *
 * 所有多字节字段为小端序。
 *
 * 典型用法：
 *
 *   static rofs_t fs;
 *   rofs_file_t f;
 *   char buf[64];
 *
 *   rofs_init(&fs);
 *   if (rofs_mount(&fs, FLASH_ROFS_OFFSET, flash_read, NULL) == ROFS_OK) {
 *       if (rofs_open(&fs, "config/device.json", &f) == ROFS_OK) {
 *           int n = rofs_read(&f, buf, sizeof(buf));
 *           rofs_close(&f);
 *       }
 *   }
 *
 * 镜像由 tools/make_rofs.py 生成后烧写到 Flash 的指定分区，
 * MCU 端不需要写入接口，也不需要垃圾回收。
 */

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 镜像魔数与格式常量 */
#define ROFS_MAGIC          0x53464F52u   /* "ROFS" */
#define ROFS_VERSION        1u
#define ROFS_SUPER_SIZE     32u
#define ROFS_RECORD_SIZE    16u
#define ROFS_END_MAGIC      0x21444E45u   /* "END!" */

/* 文件名/完整相对路径最大字节数（不含 '\0'） */
#define ROFS_MAX_NAME       255u
#define ROFS_PATH_MAX       255u

/* 记录类型 */
#define ROFS_TYPE_FILE      0x01u
#define ROFS_TYPE_DIR       0x02u

/* 错误码 */
typedef enum {
    ROFS_OK             =  0,   /* 成功 */

    ROFS_ERR_PARAM      = -1,   /* 参数非法（空指针等） */
    ROFS_ERR_IO         = -2,   /* 底层读取失败 */
    ROFS_ERR_MAGIC      = -3,   /* 魔数不匹配，不是 ROFS 镜像 */
    ROFS_ERR_VERSION    = -4,   /* 格式版本不兼容 */
    ROFS_ERR_FORMAT     = -5,   /* 头部/记录区/END 结构不合法 */
    ROFS_ERR_CRC        = -6,   /* 记录或镜像 CRC 校验失败 */
    ROFS_ERR_RANGE      = -7,   /* 偏移越界 */
    ROFS_ERR_NOT_FOUND  = -8,   /* 路径不存在 */
    ROFS_ERR_IS_DIR     = -9,   /* 目标是目录 */
    ROFS_ERR_NOT_DIR    = -10,  /* 目标不是目录 */
    ROFS_ERR_INVALID    = -11,  /* 路径非法/未挂载/句柄无效 */
    ROFS_ERR_EOF        = -12   /* 保留：读取接口在 EOF 时返回 0 */
} rofs_err_t;

/*
 * 底层只读回调。
 * offset 为“介质绝对地址”，由挂载时的 base_offset + 镜像内偏移得到；
 * 必须完整读取 size 字节，成功返回 0，失败返回负数。
 */
typedef int (*rofs_read_fn)(void *ctx, uint32_t offset,
                            void *buffer, uint32_t size);

/* 镜像信息 */
typedef struct {
    uint32_t image_size;      /* 镜像总大小 */
    uint32_t records_offset;  /* 第一条记录偏移 */
    uint32_t records_size;    /* 记录区大小 */
    uint32_t record_count;    /* 记录数量 */
    uint32_t flags;           /* 镜像标志 */
    uint16_t version;         /* 格式版本 */
} rofs_info_t;

/* 文件属性 */
typedef struct {
    uint8_t type;                  /* ROFS_TYPE_FILE / ROFS_TYPE_DIR */
    uint32_t size;                 /* 文件大小，目录为 0 */
    uint32_t time;                 /* 修改时间（Unix 秒） */
    char name[ROFS_MAX_NAME + 1];  /* 相对路径或目录项名称 */
} rofs_stat_t;

/* 目录项 */
typedef struct {
    uint8_t type;
    uint32_t size;
    char name[ROFS_MAX_NAME + 1];
} rofs_dirent_t;

/*
 * 实例与句柄。
 * 字段属于内部实现，调用者静态分配后不要直接读写。
 */
typedef struct rofs {
    uint32_t base_offset;      /* 镜像在介质中的起始地址 */
    rofs_read_fn read;         /* 只读回调 */
    void *ctx;                 /* 回调上下文 */

    uint32_t image_size;       /* 镜像总大小（来自 Super Header） */
    uint32_t records_offset;   /* 第一条记录偏移，通常 32 */
    uint32_t records_size;     /* 记录区大小，不含 END */
    uint32_t record_count;     /* 记录数量 */
    uint32_t flags;            /* 镜像标志，保留 */
    uint32_t image_crc;        /* 镜像 CRC32（计算时本字段视为 0） */
    uint16_t version;          /* 格式版本 */

    uint8_t mounted;           /* 1=已挂载 */
} rofs_t;

typedef struct rofs_file {
    rofs_t *fs;                /* 所属实例 */
    uint32_t offset;           /* Body 在镜像内的偏移 */
    uint32_t size;             /* 文件大小 */
    uint32_t position;         /* 当前读取位置，范围 [0, size] */
    uint8_t used;              /* 1=句柄占用 */
} rofs_file_t;

typedef struct rofs_dir {
    rofs_t *fs;                /* 所属实例 */
    uint32_t scan_offset;      /* 下一次扫描的记录偏移 */
    uint32_t scan_count;       /* 已扫描记录数 */
    char path[ROFS_PATH_MAX + 1];   /* 归一化目录路径，根为 "" */
    char last[ROFS_MAX_NAME + 1];   /* 上一次返回的名称，用于连续去重 */
    uint8_t last_len;          /* last 的有效长度 */
    uint8_t used;              /* 1=句柄占用 */
} rofs_dir_t;

/* ------------------------------ 生命周期 ------------------------------ */

/* 初始化实例（不访问介质） */
int rofs_init(rofs_t *fs);

/*
 * 挂载镜像。
 * base_offset : 镜像在介质中的起始地址；
 * read        : 只读回调；
 * ctx         : 回调上下文。
 * 只读取 Super Header 与 End 标记，不扫描全部记录。
 */
int rofs_mount(rofs_t *fs, uint32_t base_offset,
               rofs_read_fn read, void *ctx);

/* 卸载：清空挂载状态 */
int rofs_unmount(rofs_t *fs);

/* 获取镜像信息 */
int rofs_get_info(rofs_t *fs, rofs_info_t *info);

/*
 * 完整校验：
 *   - 逐条记录检查边界、文件名与 Body CRC；
 *   - 校验记录区结束位置与 END 标记；
 *   - 校验整镜像 CRC32（image_crc32 字段视为 0）。
 * 注意：需要遍历整个镜像，耗时与镜像大小成正比。
 */
int rofs_check(rofs_t *fs);

/* ------------------------------- 文件 ------------------------------- */

/*
 * 打开文件（只读）。
 * path 为镜像内相对路径，例如 "config/device.json"。
 * 仅支持文件；目标是目录返回 ROFS_ERR_IS_DIR。
 */
int rofs_open(rofs_t *fs, const char *path, rofs_file_t *file);

int rofs_close(rofs_file_t *file);

/* 读取：返回字节数；EOF 返回 0；失败返回负错误码 */
int32_t rofs_read(rofs_file_t *file, void *buffer, uint32_t size);

/* 定位：位置必须在 [0, size] 内，越界返回 ROFS_ERR_RANGE */
int rofs_seek(rofs_file_t *file, int32_t offset, int whence);

uint32_t rofs_tell(rofs_file_t *file);
uint32_t rofs_size(rofs_file_t *file);

/* 是否到达文件尾：1=是，0=否 */
int rofs_eof(rofs_file_t *file);

/* 存在返回 1，不存在返回 0，其他为负错误码 */
int rofs_exists(rofs_t *fs, const char *path);

/* 查询属性 */
int rofs_stat(rofs_t *fs, const char *path, rofs_stat_t *stat);

/* ------------------------------- 目录 ------------------------------- */

/*
 * 打开目录。
 * path 可取 "" 或 "/" 表示根目录，也接受带前导 '/' 的路径；
 * 目录是否存在由记录路径前缀推导。
 */
int rofs_opendir(rofs_t *fs, const char *path, rofs_dir_t *dir);

/* 读取下一项：0=成功，1=结束，负值=错误 */
int rofs_readdir(rofs_dir_t *dir, rofs_dirent_t *entry);

/* 重置遍历位置到目录开头 */
int rofs_rewinddir(rofs_dir_t *dir);

int rofs_closedir(rofs_dir_t *dir);

#ifdef __cplusplus
}
#endif

#endif /* ROFS_H */
