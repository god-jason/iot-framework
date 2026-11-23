#ifndef ROFS_H
#define ROFS_H

/*
 * ROFS - 轻量级日志结构文件系统（V1 核心实现）
 * ============================================================================
 *
 * 设计目标：
 *   - 追加写（Log-Structured）：所有更新都生成新记录，不原地覆盖；
 *   - Record 级 CRC + 独立提交标记：掉电只会丢掉未提交的尾部记录；
 *   - Inode 统一管理文件/目录，文件数据按 256B 逻辑 Chunk 存放；
 *   - 静态 RAM，不使用 malloc，句柄数量编译期固定；
 *   - 与 MCU/块设备解耦，可移植到 SPI NOR 或内部 Flash；
 *   - 所有公开接口返回 0 或负的 ROFS_ERR_* 错误码。
 *
 * Flash 布局（以 4KB 扇区为例）：
 *
 *   +----------------------+  <- start_addr
 *   |  Superblock A (4KB)  |  文件系统元信息，双备份
 *   +----------------------+
 *   |  Superblock B (4KB)  |  A 损坏时使用 B
 *   +----------------------+
 *   |  Pool Block 0        |  Block Header + Record 日志
 *   +----------------------+
 *   |  Pool Block 1        |
 *   +----------------------+
 *   |        ...           |
 *   +----------------------+
 *
 * Record 逻辑格式：
 *
 *   +------------------+
 *   | Record Header 32B|  magic/sequence/tx/id/type/size/CRC
 *   +------------------+
 *   | Payload          |  Inode 或 DATA 数据
 *   +------------------+
 *   | Padding (0xFF)   |  对齐到编程粒度
 *   +------------------+
 *   | Commit Marker 4B |  最后一次单独写入
 *   +------------------+
 *
 * 文件数据模型：
 *   - 文件大小由最新 Inode 记录决定；
 *   - 数据按 chunk_index = offset / ROFS_CHUNK_SIZE 定位；
 *   - 同一 Chunk 的更新生成更大的 generation，读取时取最新有效记录；
 *   - 截断时通过 DATA_DELETE 显式失效超出范围的 Chunk。
 *
 * 并发模型：
 *   - 若配置了 lock/unlock，所有公开 API 在进入时加锁；
 *   - 块设备回调本身不再重复加锁，避免非递归互斥量死锁。
 *
 * 当前版本未实现：
 *   - 多记录事务（rofs_tx_* 返回 NOTSUP）；
 *   - Checkpoint 快速挂载；
 *   - GC 与磨损均衡（rofs_gc 返回 NOTSUP）；
 *   - 元数据缓存：查询采用按需全盘扫描，适合小容量 V1。
 *
 * RAM 预算（32 位 MCU，典型值）：
 *   - rofs_t 约 300B；
 *   - 工作缓冲 ROFS_WORKBUF_SIZE = 256B；
 *   - 2 个文件句柄 + 1 个目录句柄；
 *   - 合计约 0.7KB，满足核心 RAM <= 1KB 目标。
 */

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 版本号，Superblock 中保存为 (MAJOR << 16) | MINOR */
#define ROFS_VERSION_MAJOR       1
#define ROFS_VERSION_MINOR       0

/* 单个路径分量最大长度（不含结尾 '\0'） */
#define ROFS_NAME_MAX            31
/* 完整路径最大长度 */
#define ROFS_PATH_MAX            127

/* 文件逻辑 Chunk 大小；文件大小不受该值限制 */
#define ROFS_CHUNK_SIZE          256
/* 调用者必须提供的工作缓冲区最小大小，用于 CRC 校验与 Chunk 合并 */
#define ROFS_WORKBUF_SIZE        256

/* 编译期句柄上限，配置中的 max_open_files/dirs 不能超过这两个值 */
#define ROFS_MAX_OPEN_FILES      2
#define ROFS_MAX_OPEN_DIRS       1

/* 根目录固定 Inode 编号 */
#define ROFS_ROOT_INODE          1
/* 无效 Inode 编号 */
#define ROFS_INVALID_INODE       0

typedef uint32_t rofs_inode_t;   /* Inode 编号 */
typedef uint32_t rofs_size_t;    /* 文件大小/偏移 */
typedef int32_t  rofs_off_t;     /* Seek 偏移 */
typedef int32_t  rofs_ssize_t;   /* 读写返回值：字节数或负错误码 */

typedef struct rofs rofs_t;           /* 文件系统实例 */
typedef struct rofs_file rofs_file_t; /* 文件句柄 */
typedef struct rofs_dir rofs_dir_t;   /* 目录句柄 */

/*
 * 错误码。
 * 约定：
 *   - 所有接口成功返回 0（ROFS_OK）；
 *   - 读写接口失败返回负错误码，成功返回实际字节数；
 *   - rofs_readdir 用 0/1 表示“读到一项/结束”，负值才是错误。
 */
typedef enum {
    ROFS_OK                = 0,   /* 成功 */

    ROFS_ERR_PARAM         = -1,  /* 参数非法 */
    ROFS_ERR_IO            = -2,  /* 底层块设备读写失败 */
    ROFS_ERR_CRC           = -3,  /* 记录 CRC 校验失败或记录未提交 */
    ROFS_ERR_FORMAT        = -4,  /* 未格式化或 Superblock 无效 */
    ROFS_ERR_VERSION       = -5,  /* 版本不兼容 */

    ROFS_ERR_NOENT         = -6,  /* 文件/目录不存在 */
    ROFS_ERR_EXIST         = -7,  /* 已存在 */
    ROFS_ERR_NOTDIR        = -8,  /* 路径中间分量不是目录 */
    ROFS_ERR_ISDIR         = -9,  /* 目标是目录 */

    ROFS_ERR_NOSPC         = -10, /* 空间不足 */
    ROFS_ERR_NOMEM         = -11, /* 句柄耗尽 */
    ROFS_ERR_BUSY          = -12, /* 忙 */
    ROFS_ERR_INVAL         = -13, /* 非法值（如路径格式错误） */

    ROFS_ERR_READONLY      = -14, /* 只读挂载 */
    ROFS_ERR_CORRUPT       = -15, /* 元数据损坏 */
    ROFS_ERR_NOTEMPTY      = -16, /* 目录非空 */
    ROFS_ERR_BADF          = -17, /* 句柄无效 */

    ROFS_ERR_NOTSUP        = -18, /* 当前版本不支持 */
    ROFS_ERR_TIMEOUT       = -19, /* 超时 */
    ROFS_ERR_STATE         = -20, /* 状态不允许（如未挂载） */
    ROFS_ERR_POWERLOSS     = -21, /* 掉电恢复相关 */
    ROFS_ERR_BADBLOCK      = -22  /* 坏块 */
} rofs_error_t;

/*
 * 块设备接口。
 * 回调均返回 0 表示成功，非 0 由 ROFS 统一映射为 ROFS_ERR_IO。
 * init/deinit 由调用者自行决定是否使用，ROFS 不会自动调用。
 */
typedef struct {
    int (*init)(void *ctx);                                              /* 可选：设备初始化 */
    int (*deinit)(void *ctx);                                            /* 可选：设备反初始化 */

    int (*read)(void *ctx, uint32_t addr, void *buffer, uint32_t size);  /* 必需：读取 */
    int (*program)(void *ctx, uint32_t addr, const void *buffer,
                   uint32_t size);                                        /* 必需：写入（只能 1->0） */
    int (*erase)(void *ctx, uint32_t addr, uint32_t size);               /* 必需：擦除，addr/size 按 erase_size 对齐 */
    int (*sync)(void *ctx);                                               /* 可选：写缓存同步 */

    void *ctx;                    /* 传给所有回调的上下文，可为 NULL */

    uint32_t start_addr;          /* 文件系统分区在介质中的起始地址 */
    uint32_t total_size;          /* 分区总大小，必须为 erase_size 整数倍，至少 3 个块 */
    uint32_t erase_size;          /* 擦除单元（Block）大小，通常 4096 */
    uint32_t program_size;        /* 编程粒度，V1 建议为 1；用于 Commit Marker 对齐 */
    uint32_t read_size;           /* 读取粒度，V1 仅作记录 */
} rofs_blockdev_t;

/*
 * 文件系统配置。
 * workbuf 与 workbuf_size 是必需的；其余字段可填 0 使用默认值。
 */
typedef struct {
    uint8_t *workbuf;             /* 工作缓冲区，至少 ROFS_WORKBUF_SIZE 字节 */
    uint32_t workbuf_size;        /* 实际缓冲区大小 */

    uint8_t max_open_files;       /* 最大打开文件数，0 或超限时取 ROFS_MAX_OPEN_FILES */
    uint8_t max_open_dirs;        /* 最大打开目录数，0 或超限时取 ROFS_MAX_OPEN_DIRS */

    uint8_t auto_gc;              /* 保留，V1 未使用 */
    uint8_t readonly;             /* 非 0 时禁止写/格式化/删除 */

    uint32_t gc_threshold;        /* 保留，用于 rofs_gc_needed 的空闲阈值 */

    void (*lock)(void *ctx);      /* 可选：进入公开 API 时加锁 */
    void (*unlock)(void *ctx);    /* 可选：离开公开 API 时解锁 */
    void *mutex_ctx;              /* 传给 lock/unlock 的上下文 */

    uint32_t (*get_time)(void);   /* 可选：返回 Unix 时间戳，用于 Inode 时间字段 */
} rofs_config_t;

/*
 * 空间统计。
 * used_bytes 按“已分配块 + 当前活动块已用区间”计算，
 * 不扣除日志中失效记录占用的空间（V1 没有 GC）。
 */
typedef struct {
    uint32_t total_bytes;         /* 分区总容量 */
    uint32_t used_bytes;          /* 已分配容量 */
    uint32_t free_bytes;          /* 尚未使用的容量（含活动块尾部） */
    uint32_t total_blocks;        /* 池内块总数 */
    uint32_t free_blocks;         /* 完全空闲的块数 */
    uint32_t block_size;          /* 块大小 */
} rofs_statfs_t;

/* 文件/目录属性 */
typedef struct {
    rofs_inode_t inode;           /* Inode 编号 */
    rofs_inode_t parent;          /* 父目录 Inode，根为 0 */
    uint32_t size;                /* 文件大小（目录为 0） */
    uint32_t version;             /* Inode 版本号，用于判定最新记录 */
    uint32_t create_time;         /* 创建时间（秒） */
    uint32_t modify_time;         /* 修改时间（秒） */
    uint8_t type;                 /* 1=文件，2=目录 */
    char name[ROFS_NAME_MAX + 1]; /* 名称，'\0' 结尾 */
} rofs_stat_t;

/* 目录项（readdir 返回） */
typedef struct {
    rofs_inode_t inode;           /* Inode 编号 */
    uint32_t size;                /* 文件大小 */
    uint8_t type;                 /* 1=文件，2=目录 */
    char name[ROFS_NAME_MAX + 1]; /* 名称 */
} rofs_dirent_t;

/*
 * 实例与句柄定义。
 * 之所以在公共头文件中给出完整定义，是为了允许调用者静态分配；
 * 字段属于内部实现，应用层不应直接读写。
 */
struct rofs_file {
    rofs_t *fs;                   /* 所属文件系统 */
    uint32_t inode;               /* 文件 Inode */
    uint32_t parent;              /* 父目录 Inode */
    uint32_t size;                /* 打开时/写入后的文件大小 */
    uint32_t pos;                 /* 当前读写位置 */
    uint32_t version;             /* 打开时 Inode 版本 */
    uint32_t epoch;               /* 数据 epoch，截断时用于失效旧 Chunk */
    uint32_t flags;               /* 打开标志 */
    uint8_t used;                 /* 句柄是否占用 */
    char name[ROFS_NAME_MAX + 1]; /* 文件名 */
};

struct rofs_dir {
    rofs_t *fs;                   /* 所属文件系统 */
    uint32_t inode;               /* 目录 Inode */
    uint32_t iter_seq;            /* 上一次返回记录的最大 sequence */
    uint8_t used;                 /* 句柄是否占用 */
};

struct rofs {
    rofs_blockdev_t dev;          /* 块设备副本 */
    rofs_config_t cfg;            /* 配置副本 */

    uint8_t *workbuf;             /* 工作缓冲区指针 */
    uint32_t workbuf_size;        /* 工作缓冲区大小 */

    uint8_t mounted;              /* 是否已挂载 */
    uint8_t readonly;             /* 只读标志 */

    uint32_t block_size;          /* 块大小（= dev.erase_size） */
    uint32_t pool_start;          /* 数据池起始绝对地址 */
    uint32_t block_count;         /* 数据池块数 */
    uint32_t generation;          /* Superblock 代次，格式化时递增 */

    uint32_t seq;                 /* 下一条记录/块头的 sequence */
    uint32_t next_inode;          /* 下一个可用 Inode 编号 */
    uint32_t next_tx;             /* 下一个事务编号（仅记录在 Record 中） */

    uint32_t active_block;        /* 当前追加块号，ROFS_BLOCK_NONE 表示无 */
    uint32_t active_off;          /* 追加块内下一个可写偏移 */
    uint32_t erase_count;         /* 当前活动块擦除次数（V1 仅记录） */

    uint32_t free_blocks;         /* 完全空闲块数 */
    uint32_t used_bytes;          /* 有效记录累计字节数（统计用） */

    rofs_file_t files[ROFS_MAX_OPEN_FILES]; /* 文件句柄池 */
    rofs_dir_t dirs[ROFS_MAX_OPEN_DIRS];    /* 目录句柄池 */
};

/* 打开标志：低 2 位为访问模式，其余可按位组合 */
#define ROFS_O_RDONLY        0x0001   /* 只读 */
#define ROFS_O_WRONLY        0x0002   /* 只写 */
#define ROFS_O_RDWR          0x0003   /* 读写 */

#define ROFS_O_CREAT         0x0004   /* 不存在则创建 */
#define ROFS_O_EXCL          0x0008   /* 与 CREAT 同用，已存在则报错 */
#define ROFS_O_APPEND        0x0010   /* 每次写前定位到文件尾 */
#define ROFS_O_TRUNC         0x0020   /* 打开时清空文件 */

/* Seek 基准 */
#define ROFS_SEEK_SET        0        /* 从文件头开始 */
#define ROFS_SEEK_CUR        1        /* 从当前位置开始 */
#define ROFS_SEEK_END        2        /* 从文件尾开始 */

/* ------------------------------ 生命周期 ------------------------------ */

/*
 * 初始化文件系统实例（不访问 Flash）。
 * fs     : 调用者静态分配的实例；
 * dev    : 块设备描述，必须提供 read/program/erase；
 * config : 配置，workbuf 至少 ROFS_WORKBUF_SIZE 字节。
 * 返回   : ROFS_OK / ROFS_ERR_PARAM。
 */
int rofs_init(rofs_t *fs, const rofs_blockdev_t *dev, const rofs_config_t *config);

/*
 * 格式化分区并写入 Superblock、Block 0 和根 Inode。
 * 会擦除 Superblock 区和整个数据池，耗时与容量成正比。
 * 成功后文件系统处于已挂载状态。
 */
int rofs_format(rofs_t *fs);

/*
 * 挂载：读取 Superblock，扫描所有块恢复 sequence/Inode/活动块，
 * 并校验根目录。未格式化时返回 ROFS_ERR_FORMAT。
 */
int rofs_mount(rofs_t *fs);

/* 卸载：同步并释放所有句柄；未挂载时也返回 ROFS_OK */
int rofs_unmount(rofs_t *fs);

/* 将底层缓存同步到介质 */
int rofs_sync(rofs_t *fs);

/* 基础自检：校验 Superblock 并扫描所有有效 Record */
int rofs_check(rofs_t *fs);

/* 查询空间统计 */
int rofs_statfs(rofs_t *fs, rofs_statfs_t *stat);

/* ------------------------------ 文件操作 ------------------------------ */

/*
 * 打开文件。
 * flags : ROFS_O_* 组合，访问模式必须是 RDONLY/WRONLY/RDWR 之一；
 * file  : 成功时返回内部句柄池中的指针，无需释放内存。
 * 返回  : ROFS_OK / ROFS_ERR_NOENT / ROFS_ERR_ISDIR / ROFS_ERR_NOMEM 等。
 */
int rofs_open(rofs_t *fs, const char *path, uint32_t flags, rofs_file_t **file);

/* 关闭文件并释放句柄；不自动提交未完成的 Inode 更新 */
int rofs_close(rofs_file_t *file);

/* 读取：返回字节数；EOF 返回 0；失败返回负错误码 */
rofs_ssize_t rofs_read(rofs_file_t *file, void *buffer, uint32_t size);

/*
 * 写入：返回实际写入字节数；失败返回负错误码。
 * 每个 DATA 记录独立提交；最后若文件变大，再追加一条 Inode 更新。
 */
rofs_ssize_t rofs_write(rofs_file_t *file, const void *buffer, uint32_t size);

/* 定位：允许 seek 到文件尾之后，读取空洞返回 0 */
int rofs_seek(rofs_file_t *file, rofs_off_t offset, int whence);

/* 当前读写位置；句柄无效返回负错误码 */
rofs_off_t rofs_tell(rofs_file_t *file);

/* 当前文件大小；句柄无效返回 0 */
rofs_size_t rofs_size(rofs_file_t *file);

/*
 * 截断文件。
 * V1 仅支持缩小：会改写边界 Chunk 并写 DATA_DELETE 失效后续 Chunk；
 * 扩展请使用 rofs_write。
 */
int rofs_truncate(rofs_file_t *file, rofs_size_t size);

/* 同步底层介质 */
int rofs_fsync(rofs_file_t *file);

/* --------------------------- 目录与文件管理 --------------------------- */

/* 创建目录；路径中间目录必须已存在 */
int rofs_mkdir(rofs_t *fs, const char *path);

/* 删除空目录；非空返回 ROFS_ERR_NOTEMPTY */
int rofs_rmdir(rofs_t *fs, const char *path);

/* 删除文件；目标是目录返回 ROFS_ERR_ISDIR */
int rofs_unlink(rofs_t *fs, const char *path);

/*
 * 重命名/移动文件或目录。
 * V1 若目标已存在则返回 ROFS_ERR_EXIST；
 * 目录不能移动到自身子树。
 */
int rofs_rename(rofs_t *fs, const char *oldpath, const char *newpath);

/* 存在返回 1，不存在返回 0，其他为负错误码 */
int rofs_exists(rofs_t *fs, const char *path);

/* 查询属性 */
int rofs_stat(rofs_t *fs, const char *path, rofs_stat_t *stat);

/* 是目录返回 1，否则返回 0，其他为负错误码 */
int rofs_isdir(rofs_t *fs, const char *path);

/* ------------------------------ 目录遍历 ------------------------------ */

/* 打开目录；句柄来自内部池 */
int rofs_opendir(rofs_t *fs, const char *path, rofs_dir_t **dir);

/*
 * 读取下一个目录项。
 * 返回 0：成功填入 entry；
 * 返回 1：遍历结束；
 * 返回负值：错误。
 * 遍历顺序为各 Inode 最新记录在日志中的先后顺序。
 */
int rofs_readdir(rofs_dir_t *dir, rofs_dirent_t *entry);

/* 关闭目录句柄 */
int rofs_closedir(rofs_dir_t *dir);

/* --------------------------- 事务（V1 未实现） --------------------------- */

int rofs_tx_begin(rofs_t *fs);   /* 返回 ROFS_ERR_NOTSUP */
int rofs_tx_commit(rofs_t *fs);  /* 返回 ROFS_ERR_NOTSUP */
int rofs_tx_abort(rofs_t *fs);   /* 返回 ROFS_ERR_NOTSUP */

/* ------------------------ 维护接口（V1 部分实现） ------------------------ */

int rofs_gc(rofs_t *fs);         /* 返回 ROFS_ERR_NOTSUP */
int rofs_gc_needed(rofs_t *fs);  /* 根据 gc_threshold 返回 0/1 */
int rofs_checkpoint(rofs_t *fs); /* 返回 ROFS_ERR_NOTSUP */

/* 剩余/总空间快捷接口 */
int rofs_free_space(rofs_t *fs, uint32_t *size);
int rofs_total_space(rofs_t *fs, uint32_t *size);

/* 统计各块擦除次数；无有效块返回 ROFS_ERR_NOENT */
int rofs_erase_stat(rofs_t *fs, uint32_t *min_count,
                    uint32_t *max_count, uint32_t *avg_count);

/* 错误码转字符串 */
const char *rofs_strerror(int error);

#ifdef __cplusplus
}
#endif

#endif /* ROFS_H */
