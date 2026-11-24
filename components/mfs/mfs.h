#ifndef MFS_H
#define MFS_H

/*
 * MFS - 轻量级日志结构文件系统（V1 核心实现）
 * ============================================================================
 *
 * 设计目标：
 *   - 追加写（Log-Structured）：所有更新都生成新记录，不原地覆盖；
 *   - Record 级 CRC + 独立提交标记：掉电只会丢掉未提交的尾部记录；
 *   - Inode 统一管理文件/目录，文件数据按 512B 逻辑 Chunk 存放；
 *   - 静态 RAM，不使用 malloc，句柄数量编译期固定；
 *   - 与 MCU/块设备解耦，可移植到 SPI NOR 或内部 Flash；
 *   - 所有公开接口返回 0 或负的 MFS_ERR_* 错误码。
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
 *   - 数据按 chunk_index = offset / MFS_CHUNK_SIZE 定位；
 *   - 同一 Chunk 的更新生成更大的 generation，读取时取最新有效记录；
 *   - 截断时通过 DATA_DELETE 显式失效超出范围的 Chunk。
 *
 * 并发模型：
 *   - 若配置了 lock/unlock，所有公开 API 在进入时加锁；
 *   - 块设备回调本身不再重复加锁，避免非递归互斥量死锁。
 *
 * 当前版本未实现：
 *   - 多记录事务（mfs_tx_* 返回 NOTSUP）；
 *   - Checkpoint 快速挂载；
 *   - 元数据缓存：查询采用按需全盘扫描，适合小容量 V1。
 *
 * V1.1 已实现：
 *   - Block GC：按有效数据比例选择牺牲块，先复制有效记录再擦除；
 *   - 动态磨损均衡：块头持久化擦除次数，优先分配低磨损空闲块；
 *   - 坏块管理：块头 BAD 状态持久化，分配/GC 自动跳过。
 *
 * RAM 预算（32 位 MCU，典型值）：
 *   - mfs_t 约 300B；
 *   - 工作缓冲 MFS_WORKBUF_SIZE = 512B；
 *   - 2 个文件句柄 + 1 个目录句柄；
 *   - 合计约 1KB（GC 内部另有约 3KB 静态候选表）。
 */

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 版本号，Superblock 中保存为 (MAJOR << 16) | MINOR */
#define MFS_VERSION_MAJOR       1
#define MFS_VERSION_MINOR       2       /* 1.2：Chunk 512B，旧盘自动重格式化 */

/* 单个路径分量最大长度（不含结尾 '\0'） */
#define MFS_NAME_MAX            31
/* 完整路径最大长度 */
#define MFS_PATH_MAX            127

/*
 * 文件逻辑 Chunk 大小；文件大小不受该值限制。
 * 512B 与 4KB 块配合时每块可放 7 条记录（每块浪费 < 2%），
 * 数据区利用率约 87.5%（256B Chunk 时只有 75%）。
 */
#define MFS_CHUNK_SIZE          512
/* 调用者必须提供的工作缓冲区最小大小，用于 CRC 校验与 Chunk 合并 */
#define MFS_WORKBUF_SIZE        MFS_CHUNK_SIZE

/* 编译期句柄上限，配置中的 max_open_files/dirs 不能超过这两个值 */
#define MFS_MAX_OPEN_FILES      2
#define MFS_MAX_OPEN_DIRS       1

/* 根目录固定 Inode 编号 */
#define MFS_ROOT_INODE          1
/* 无效 Inode 编号 */
#define MFS_INVALID_INODE       0

typedef uint32_t mfs_inode_t;   /* Inode 编号 */
typedef uint32_t mfs_size_t;    /* 文件大小/偏移 */
typedef int32_t  mfs_off_t;     /* Seek 偏移 */
typedef int32_t  mfs_ssize_t;   /* 读写返回值：字节数或负错误码 */

typedef struct mfs mfs_t;           /* 文件系统实例 */
typedef struct mfs_file mfs_file_t; /* 文件句柄 */
typedef struct mfs_dir mfs_dir_t;   /* 目录句柄 */

/*
 * 错误码。
 * 约定：
 *   - 所有接口成功返回 0（MFS_OK）；
 *   - 读写接口失败返回负错误码，成功返回实际字节数；
 *   - mfs_readdir 用 0/1 表示“读到一项/结束”，负值才是错误。
 */
typedef enum {
    MFS_OK                = 0,   /* 成功 */

    MFS_ERR_PARAM         = -1,  /* 参数非法 */
    MFS_ERR_IO            = -2,  /* 底层块设备读写失败 */
    MFS_ERR_CRC           = -3,  /* 记录 CRC 校验失败或记录未提交 */
    MFS_ERR_FORMAT        = -4,  /* 未格式化或 Superblock 无效 */
    MFS_ERR_VERSION       = -5,  /* 版本不兼容 */

    MFS_ERR_NOENT         = -6,  /* 文件/目录不存在 */
    MFS_ERR_EXIST         = -7,  /* 已存在 */
    MFS_ERR_NOTDIR        = -8,  /* 路径中间分量不是目录 */
    MFS_ERR_ISDIR         = -9,  /* 目标是目录 */

    MFS_ERR_NOSPC         = -10, /* 空间不足 */
    MFS_ERR_NOMEM         = -11, /* 句柄耗尽 */
    MFS_ERR_BUSY          = -12, /* 忙 */
    MFS_ERR_INVAL         = -13, /* 非法值（如路径格式错误） */

    MFS_ERR_READONLY      = -14, /* 只读挂载 */
    MFS_ERR_CORRUPT       = -15, /* 元数据损坏 */
    MFS_ERR_NOTEMPTY      = -16, /* 目录非空 */
    MFS_ERR_BADF          = -17, /* 句柄无效 */

    MFS_ERR_NOTSUP        = -18, /* 当前版本不支持 */
    MFS_ERR_TIMEOUT       = -19, /* 超时 */
    MFS_ERR_STATE         = -20, /* 状态不允许（如未挂载） */
    MFS_ERR_POWERLOSS     = -21, /* 掉电恢复相关 */
    MFS_ERR_BADBLOCK      = -22  /* 坏块 */
} mfs_error_t;

/*
 * 块设备接口。
 * 回调均返回 0 表示成功，非 0 由 MFS 统一映射为 MFS_ERR_IO。
 * init/deinit 由调用者自行决定是否使用，MFS 不会自动调用。
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
} mfs_blockdev_t;

/*
 * 文件系统配置。
 * workbuf 与 workbuf_size 是必需的；其余字段可填 0 使用默认值。
 */
typedef struct {
    uint8_t *workbuf;             /* 工作缓冲区，至少 MFS_WORKBUF_SIZE 字节 */
    uint32_t workbuf_size;        /* 实际缓冲区大小 */

    uint8_t max_open_files;       /* 最大打开文件数，0 或超限时取 MFS_MAX_OPEN_FILES */
    uint8_t max_open_dirs;        /* 最大打开目录数，0 或超限时取 MFS_MAX_OPEN_DIRS */

    uint8_t auto_gc;              /* 非 0 时写满且无空闲块会自动触发一次 GC */
    uint8_t readonly;             /* 非 0 时禁止写/格式化/删除 */

    uint32_t gc_threshold;        /* 保留，用于 mfs_gc_needed 的空闲阈值 */

    void (*lock)(void *ctx);      /* 可选：进入公开 API 时加锁 */
    void (*unlock)(void *ctx);    /* 可选：离开公开 API 时解锁 */
    void *mutex_ctx;              /* 传给 lock/unlock 的上下文 */

    uint32_t (*get_time)(void);   /* 可选：返回 Unix 时间戳，用于 Inode 时间字段 */
} mfs_config_t;

/*
 * 空间统计。
 * used_bytes 按已分配块计算；失效记录占比由 GC 回收，
 * bad_blocks 不计入可用容量。
 */
typedef struct {
    uint32_t total_bytes;         /* 分区总容量 */
    uint32_t used_bytes;          /* 已分配容量 */
    uint32_t free_bytes;          /* 尚未使用的容量（含活动块尾部） */
    uint32_t total_blocks;        /* 池内块总数 */
    uint32_t free_blocks;         /* 完全空闲的块数 */
    uint32_t bad_blocks;          /* 已标记为坏块的块数 */
    uint32_t block_size;          /* 块大小 */
} mfs_statfs_t;

/* 文件/目录属性 */
typedef struct {
    mfs_inode_t inode;           /* Inode 编号 */
    mfs_inode_t parent;          /* 父目录 Inode，根为 0 */
    uint32_t size;                /* 文件大小（目录为 0） */
    uint32_t version;             /* Inode 版本号，用于判定最新记录 */
    uint32_t create_time;         /* 创建时间（秒） */
    uint32_t modify_time;         /* 修改时间（秒） */
    uint8_t type;                 /* 1=文件，2=目录 */
    char name[MFS_NAME_MAX + 1]; /* 名称，'\0' 结尾 */
} mfs_stat_t;

/* 目录项（readdir 返回） */
typedef struct {
    mfs_inode_t inode;           /* Inode 编号 */
    uint32_t size;                /* 文件大小 */
    uint8_t type;                 /* 1=文件，2=目录 */
    char name[MFS_NAME_MAX + 1]; /* 名称 */
} mfs_dirent_t;

/*
 * 实例与句柄定义。
 * 之所以在公共头文件中给出完整定义，是为了允许调用者静态分配；
 * 字段属于内部实现，应用层不应直接读写。
 */
struct mfs_file {
    mfs_t *fs;                   /* 所属文件系统 */
    uint32_t inode;               /* 文件 Inode */
    uint32_t parent;              /* 父目录 Inode */
    uint32_t size;                /* 打开时/写入后的文件大小 */
    uint32_t pos;                 /* 当前读写位置 */
    uint32_t version;             /* 打开时 Inode 版本 */
    uint32_t epoch;               /* 数据 epoch，截断时用于失效旧 Chunk */
    uint32_t flags;               /* 打开标志 */
    uint8_t used;                 /* 句柄是否占用 */
    char name[MFS_NAME_MAX + 1]; /* 文件名 */
};

struct mfs_dir {
    mfs_t *fs;                   /* 所属文件系统 */
    uint32_t inode;               /* 目录 Inode */
    uint32_t iter_seq;            /* 上一次返回记录的最大 sequence */
    uint8_t used;                 /* 句柄是否占用 */
};

struct mfs {
    mfs_blockdev_t dev;          /* 块设备副本 */
    mfs_config_t cfg;            /* 配置副本 */

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

    uint32_t active_block;        /* 当前追加块号，MFS_BLOCK_NONE 表示无 */
    uint32_t active_off;          /* 追加块内下一个可写偏移 */
    uint32_t erase_count;         /* 当前活动块擦除次数 */

    uint32_t free_blocks;         /* 完全空闲块数（不含坏块） */
    uint32_t bad_blocks;          /* 已标记坏块数 */
    uint32_t used_bytes;          /* 已分配容量（按块统计，不含坏块） */
    uint32_t alloc_hint;          /* 磨损均衡轮转起点 */
    uint32_t gc_block;            /* 正在 GC 的牺牲块，MFS_BLOCK_NONE 表示无 */
    uint8_t  gc_running;          /* GC 运行标志，防止递归 */

    mfs_file_t files[MFS_MAX_OPEN_FILES]; /* 文件句柄池 */
    mfs_dir_t dirs[MFS_MAX_OPEN_DIRS];    /* 目录句柄池 */
};

/* 打开标志：低 2 位为访问模式，其余可按位组合 */
#define MFS_O_RDONLY        0x0001   /* 只读 */
#define MFS_O_WRONLY        0x0002   /* 只写 */
#define MFS_O_RDWR          0x0003   /* 读写 */

#define MFS_O_CREAT         0x0004   /* 不存在则创建 */
#define MFS_O_EXCL          0x0008   /* 与 CREAT 同用，已存在则报错 */
#define MFS_O_APPEND        0x0010   /* 每次写前定位到文件尾 */
#define MFS_O_TRUNC         0x0020   /* 打开时清空文件 */

/* Seek 基准 */
#define MFS_SEEK_SET        0        /* 从文件头开始 */
#define MFS_SEEK_CUR        1        /* 从当前位置开始 */
#define MFS_SEEK_END        2        /* 从文件尾开始 */

/* ------------------------------ 生命周期 ------------------------------ */

/*
 * 初始化文件系统实例（不访问 Flash）。
 * fs     : 调用者静态分配的实例；
 * dev    : 块设备描述，必须提供 read/program/erase；
 * config : 配置，workbuf 至少 MFS_WORKBUF_SIZE 字节。
 * 返回   : MFS_OK / MFS_ERR_PARAM。
 */
int mfs_init(mfs_t *fs, const mfs_blockdev_t *dev, const mfs_config_t *config);

/*
 * 格式化分区并写入 Superblock、Block 0 和根 Inode。
 * 会擦除 Superblock 区和整个数据池，耗时与容量成正比。
 * 成功后文件系统处于已挂载状态。
 */
int mfs_format(mfs_t *fs);

/*
 * 挂载：读取 Superblock，扫描所有块恢复 sequence/Inode/活动块，
 * 并校验根目录。未格式化时返回 MFS_ERR_FORMAT。
 */
int mfs_mount(mfs_t *fs);

/* 卸载：同步并释放所有句柄；未挂载时也返回 MFS_OK */
int mfs_unmount(mfs_t *fs);

/* 将底层缓存同步到介质 */
int mfs_sync(mfs_t *fs);

/* 基础自检：校验 Superblock 并扫描所有有效 Record */
int mfs_check(mfs_t *fs);

/* 查询空间统计 */
int mfs_statfs(mfs_t *fs, mfs_statfs_t *stat);

/* ------------------------------ 文件操作 ------------------------------ */

/*
 * 打开文件。
 * flags : MFS_O_* 组合，访问模式必须是 RDONLY/WRONLY/RDWR 之一；
 * file  : 成功时返回内部句柄池中的指针，无需释放内存。
 * 返回  : MFS_OK / MFS_ERR_NOENT / MFS_ERR_ISDIR / MFS_ERR_NOMEM 等。
 */
int mfs_open(mfs_t *fs, const char *path, uint32_t flags, mfs_file_t **file);

/* 关闭文件并释放句柄；不自动提交未完成的 Inode 更新 */
int mfs_close(mfs_file_t *file);

/* 读取：返回字节数；EOF 返回 0；失败返回负错误码 */
mfs_ssize_t mfs_read(mfs_file_t *file, void *buffer, uint32_t size);

/*
 * 写入：返回实际写入字节数；失败返回负错误码。
 * 每个 DATA 记录独立提交；最后若文件变大，再追加一条 Inode 更新。
 */
mfs_ssize_t mfs_write(mfs_file_t *file, const void *buffer, uint32_t size);

/* 定位：允许 seek 到文件尾之后，读取空洞返回 0 */
int mfs_seek(mfs_file_t *file, mfs_off_t offset, int whence);

/* 当前读写位置；句柄无效返回负错误码 */
mfs_off_t mfs_tell(mfs_file_t *file);

/* 当前文件大小；句柄无效返回 0 */
mfs_size_t mfs_size(mfs_file_t *file);

/*
 * 截断文件。
 * V1 仅支持缩小：会改写边界 Chunk 并写 DATA_DELETE 失效后续 Chunk；
 * 扩展请使用 mfs_write。
 */
int mfs_truncate(mfs_file_t *file, mfs_size_t size);

/* 同步底层介质 */
int mfs_fsync(mfs_file_t *file);

/* --------------------------- 目录与文件管理 --------------------------- */

/* 创建目录；路径中间目录必须已存在 */
int mfs_mkdir(mfs_t *fs, const char *path);

/* 删除空目录；非空返回 MFS_ERR_NOTEMPTY */
int mfs_rmdir(mfs_t *fs, const char *path);

/* 删除文件；目标是目录返回 MFS_ERR_ISDIR */
int mfs_unlink(mfs_t *fs, const char *path);

/*
 * 重命名/移动文件或目录。
 * V1 若目标已存在则返回 MFS_ERR_EXIST；
 * 目录不能移动到自身子树。
 */
int mfs_rename(mfs_t *fs, const char *oldpath, const char *newpath);

/* 存在返回 1，不存在返回 0，其他为负错误码 */
int mfs_exists(mfs_t *fs, const char *path);

/* 查询属性 */
int mfs_stat(mfs_t *fs, const char *path, mfs_stat_t *stat);

/* 是目录返回 1，否则返回 0，其他为负错误码 */
int mfs_isdir(mfs_t *fs, const char *path);

/* ------------------------------ 目录遍历 ------------------------------ */

/* 打开目录；句柄来自内部池 */
int mfs_opendir(mfs_t *fs, const char *path, mfs_dir_t **dir);

/*
 * 读取下一个目录项。
 * 返回 0：成功填入 entry；
 * 返回 1：遍历结束；
 * 返回负值：错误。
 * 遍历顺序为各 Inode 最新记录在日志中的先后顺序。
 */
int mfs_readdir(mfs_dir_t *dir, mfs_dirent_t *entry);

/* 关闭目录句柄 */
int mfs_closedir(mfs_dir_t *dir);

/* --------------------------- 事务（V1 未实现） --------------------------- */

int mfs_tx_begin(mfs_t *fs);   /* 返回 MFS_ERR_NOTSUP */
int mfs_tx_commit(mfs_t *fs);  /* 返回 MFS_ERR_NOTSUP */
int mfs_tx_abort(mfs_t *fs);   /* 返回 MFS_ERR_NOTSUP */

/* ------------------------ 维护接口（V1.1） ------------------------ */

int mfs_gc(mfs_t *fs);         /* 手动 GC，回收失效记录 */
int mfs_gc_needed(mfs_t *fs);  /* 根据空闲空间返回 0/1 */
int mfs_checkpoint(mfs_t *fs); /* 返回 MFS_ERR_NOTSUP */

/*
 * 将指定池内块标记为坏块（会擦除该块，原有数据丢失）。
 * 用于已知损坏或擦除失败的块；标记后分配与 GC 都会跳过。
 * block 不合法、是活动块或正在 GC 时返回错误。
 */
int mfs_mark_bad(mfs_t *fs, uint32_t block);

/* 剩余/总空间快捷接口 */
int mfs_free_space(mfs_t *fs, uint32_t *size);
int mfs_total_space(mfs_t *fs, uint32_t *size);

/* 统计各块擦除次数；无有效块返回 MFS_ERR_NOENT */
int mfs_erase_stat(mfs_t *fs, uint32_t *min_count,
                    uint32_t *max_count, uint32_t *avg_count);

/* 错误码转字符串 */
const char *mfs_strerror(int error);

#ifdef __cplusplus
}
#endif

#endif /* MFS_H */
