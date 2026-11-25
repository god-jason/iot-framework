#ifndef FKV_H
#define FKV_H

/*
 * fkv.h - 双区滚动日志键值存储（u32 key → 小数据）
 * ============================================================================
 *
 * 设计参照：
 *   - platforms/stm32/bootloader/boot_meta.c：分区状态 + CRC + 追加历史；
 *   - components/config/config.c：基于本组件的配置读写封装；
 *
 * 用途：保存配置、标定参数、运行计数等「小数据」，不是通用文件系统。
 * 每个键对应一条记录，更新/删除都追加新记录，不原地覆盖。
 *
 * 分区布局（两个等大分区 A/B，region = total_size / 2）：
 *
 *   +---------------------------+  <- region_base
 *   | SUPER (32B，最后写入)      |
 *   +---------------------------+  <- region_base + 32
 *   | 记录 1 (key/value)        |  顺序追加
 *   | 记录 2 ...                |
 *   | 0xFF ...                  |  空尾
 *   +---------------------------+
 *
 * 记录逻辑格式：
 *
 *   +----------+---------+-------------+----------+
 *   | Header   | Payload | Padding     | Commit   |
 *   | 24B      | Len B   | 对齐到编程粒度 | 4B(最后写)|
 *   +----------+---------+-------------+----------+
 *
 *   Header：magic/version/length/sequence/crc32/key/flags
 *   CRC32 覆盖「Header（crc 字段按 0）+ Payload」。
 *
 * 掉电安全 / 滚动切换：
 *   - 单条记录：先写 Header+Payload 并回读校验，最后写 Commit；
 *   - 分区提交：压缩时先复制全部有效记录，最后写 SUPER（代次 +1）；
 *   - 挂载只选择 SUPER 已提交且代次更大的分区，因此掉电不会
 *     把“拷贝到一半”的分区当成有效数据；
 *   - 切换完成后擦除旧分区，两块交替使用，天然均摊擦除次数。
 *
 * 键的数量上限为 FKV_MAX_KEYS（压缩时需要常驻槽位表判定最新值），
 * 超出后写入仍可进行，但压缩会返回 FKV_ERR_NOSPC。
 *
 * 本组件只实现上层功能与算法：块设备通过回调注入，
 * 具体 Flash 地址与擦写由调用者（app 适配层）提供。
 */

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================== 容量与限制 ============================== */

#define FKV_VERSION             1u

/* 单条 value 最大长度（Header.length 为 16 位，这里取更保守的值） */
#define FKV_VALUE_MAX           1024u

/* 同时支持的键数量上限（压缩/遍历用的静态槽位表大小） */
#ifndef FKV_MAX_KEYS
#define FKV_MAX_KEYS            32u
#endif

/* 分区最小容量 */
#define FKV_MIN_REGION_SIZE     512u

/* ============================== 错误码 ============================== */

typedef enum {
    FKV_OK              = 0,    /* 成功 */

    FKV_ERR_PARAM       = -1,   /* 参数非法 */
    FKV_ERR_IO          = -2,   /* 块设备读写/擦除失败 */
    FKV_ERR_CRC         = -3,   /* 记录 CRC 或 Commit 校验失败 */
    FKV_ERR_FORMAT      = -4,   /* 未格式化 / SUPER 无效 */
    FKV_ERR_VERSION     = -5,   /* 格式版本不兼容 */

    FKV_ERR_NOENT       = -6,   /* 键不存在 */
    FKV_ERR_NOSPC       = -7,   /* 空间不足 / 键数量超限 */
    FKV_ERR_NOMEM       = -8,   /* 工作缓冲不足 */
    FKV_ERR_INVAL       = -9,   /* 非法值（长度超限等） */

    FKV_ERR_READONLY    = -10,  /* 只读挂载 */
    FKV_ERR_CORRUPT     = -11,  /* 元数据损坏 */
    FKV_ERR_STATE       = -12   /* 状态不允许（未挂载等） */
} fkv_error_t;

/* ============================== 块设备 ============================== */

/*
 * 块设备回调。所有地址为介质的绝对字节地址。
 *   read    ：任意地址/长度读取；
 *   program ：写入（只能把 1 变成 0，调用者需保证目标已擦除）；
 *   erase   ：擦除，addr/size 必须按 erase_size 对齐。
 * 回调返回 0 成功，非 0 统一映射为 FKV_ERR_IO。
 */
typedef struct {
    int (*read)(void *ctx, uint32_t addr, void *buffer, uint32_t size);
    int (*program)(void *ctx, uint32_t addr, const void *buffer, uint32_t size);
    int (*erase)(void *ctx, uint32_t addr, uint32_t size);
    int (*sync)(void *ctx);                 /* 可选 */

    void *ctx;                              /* 回调上下文 */

    uint32_t start_addr;                    /* 存储分区起始地址 */
    uint32_t total_size;                    /* 分区总大小，按 erase_size 对齐 */
    uint32_t erase_size;                    /* 最小擦除单元 */
    uint32_t program_size;                  /* 编程粒度（<4 按 4 处理） */
} fkv_blockdev_t;

/* ============================== 配置 ============================== */

typedef struct {
    uint8_t *workbuf;                       /* 必需，大小 >= max_value */
    uint32_t workbuf_size;

    uint32_t max_value;                     /* 单条数据上限，0 或超限时取 FKV_VALUE_MAX */

    uint8_t readonly;                       /* 非 0：禁止写/删除/格式化 */
} fkv_config_t;

/* ============================== 统计 ============================== */

/*
 * 空间统计。
 * 任意时刻只有活动区可追加：
 *   total_bytes = region_size - SUPER 大小（单区可容纳容量）；
 *   used_bytes  = 活动区已追加字节数（含压缩后才回收的旧记录）；
 *   free_bytes  = total_bytes - used_bytes；
 *   total_blocks = 2（A/B 两区），used_blocks = 1，block_size = region_size。
 */
typedef struct {
    uint32_t total_bytes;
    uint32_t used_bytes;
    uint32_t free_bytes;
    uint32_t total_blocks;
    uint32_t used_blocks;
    uint32_t block_size;
} fkv_statfs_t;

/* 遍历回调：返回值非 0 时提前结束遍历 */
typedef int (*fkv_visit_cb)(void *ctx, uint32_t key,
                            const void *value, uint32_t len);

/* ============================== 实例 ============================== */

typedef struct fkv {
    fkv_blockdev_t dev;
    fkv_config_t cfg;

    uint8_t *workbuf;
    uint32_t workbuf_size;
    uint32_t max_value;

    uint8_t mounted;
    uint8_t readonly;

    uint32_t region_base[2];                /* A/B 分区起始绝对地址 */
    uint32_t region_size;                   /* 单个分区大小 */

    uint8_t active;                         /* 当前追加分区：0/1 */
    uint32_t write_off;                     /* 下一条记录偏移（>=32） */
    uint32_t generation;                    /* 当前分区代次 */
    uint32_t seq;                           /* 下一条记录序号 */
    uint32_t record_count;                  /* 活动区记录数（诊断） */
} fkv_t;

/* ============================== 生命周期 ============================== */

/*
 * 初始化实例（不访问介质）。
 * dev 必须提供 read/program/erase；cfg.workbuf 大小需 >= max_value。
 */
int fkv_init(fkv_t *kv, const fkv_blockdev_t *dev, const fkv_config_t *cfg);

/*
 * 格式化：擦除两个分区，写入 A 区 SUPER（代次 1）。
 * 成功后处于已挂载状态。
 */
int fkv_format(fkv_t *kv);

/*
 * 挂载：读取 A/B 两区 SUPER，选择代次更大的已提交分区并扫描日志。
 * 未格式化返回 FKV_ERR_FORMAT；版本不匹配返回 FKV_ERR_VERSION。
 */
int fkv_mount(fkv_t *kv);

/* 卸载：清理挂载状态（不擦除数据） */
int fkv_unmount(fkv_t *kv);

/* 同步块设备缓存；未提供 sync 回调时返回 FKV_OK */
int fkv_sync(fkv_t *kv);

/*
 * 完整性检查：逐条校验记录 Commit 与 CRC（会读取全部数据）。
 * 返回 FKV_OK / FKV_ERR_CORRUPT / FKV_ERR_IO。
 */
int fkv_check(fkv_t *kv);

int fkv_statfs(fkv_t *kv, fkv_statfs_t *stat);

/* 手动触发一次分区滚动压缩（把有效记录搬到另一区并擦除旧区） */
int fkv_gc(fkv_t *kv);

/* ============================== 键值操作 ============================== */

/*
 * 写入/覆盖一个键。value 可以为 NULL（len 必须为 0）。
 * 追加一条新记录，不覆盖旧记录。
 */
int fkv_put(fkv_t *kv, uint32_t key, const void *value, uint32_t len);

/*
 * 读取键的最新值。
 *   buf/cap ：输出缓冲（可为 NULL，此时只当查询存在性使用）；
 *   len     ：可空，返回实际长度；
 * cap 不足以容纳时返回 FKV_ERR_NOMEM，并仍然输出实际长度。
 */
int fkv_get(fkv_t *kv, uint32_t key, void *buf, uint32_t cap, uint32_t *len);

/* 查询键是否存在：1=存在，0=不存在，负值=错误 */
int fkv_exists(fkv_t *kv, uint32_t key);

/* 查询值的长度；键不存在返回 FKV_ERR_NOENT */
int fkv_size(fkv_t *kv, uint32_t key, uint32_t *len);

/* 删除键：追加一条墓碑记录；键不存在返回 FKV_ERR_NOENT */
int fkv_del(fkv_t *kv, uint32_t key);

/*
 * 遍历当前所有存活的键（顺序由记录序号决定）。
 * 回调返回非 0 时立即停止，该返回值原样返回。
 */
int fkv_foreach(fkv_t *kv, fkv_visit_cb cb, void *ctx);

/* 错误码转可读字符串 */
const char *fkv_strerror(int error);

#ifdef __cplusplus
}
#endif

#endif /* FKV_H */
