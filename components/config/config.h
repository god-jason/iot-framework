#ifndef CONFIG_H
#define CONFIG_H

/*
 * config.h - 双区轮转、追加日志式配置存储
 * ============================================================================
 *
 * 设计要点：
 *   - 把 C struct 原始内存直接写入 Flash（不做序列化）；
 *   - 两个可独立擦除的扇区（A/B）轮流追加，写满才擦另一区，磨损均衡；
 *   - 每条记录 = 记录头 + 数据 + Commit 标志，Commit 最后写入；
 *     掉电时未提交的记录不会被识别，上一次完整配置仍保留；
 *   - 启动时扫描两区，取 sequence 最大的有效记录（有符号差值处理回绕）；
 *   - 记录带 version/length，便于结构体升级时做迁移；
 *   - 不使用动态内存，RAM 只保存扫描结果（扇区、偏移、序号）。
 *
 * 记录在 Flash 中的布局：
 *   +--------------+---------+---------+----------+---------+----------+
 *   | Magic 4B     | Ver 2B  | Len 2B  | Seq 4B   | CRC32 4B| Rsv 4B   |
 *   +--------------+---------+---------+----------+---------+----------+
 *   | Data (Len B)              | Padding      | Commit 4B              |
 *   +---------------------------+--------------+------------------------+
 *
 * CRC32 覆盖“记录头（CRC 字段按 0）+ 完整数据”。
 */

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CONFIG_STORE_MAGIC      0x524F4653U     /* 'ROFS' */
#define CONFIG_STORE_COMMIT     0x434F4D54U     /* 'COMT' */
#define CONFIG_STORE_ERASED     0xFFFFFFFFU

/* 返回码（负数为错误；CONFIG_STORE_NONE 表示尚无有效配置） */
#define CONFIG_STORE_OK         0
#define CONFIG_STORE_NONE       1
#define CONFIG_STORE_ERR_PARAM  (-1)
#define CONFIG_STORE_ERR_IO     (-2)
#define CONFIG_STORE_ERR_SIZE   (-3)
#define CONFIG_STORE_ERR_VERIFY (-4)

/* 记录头（20 字节，保持自然对齐，勿随意调整字段顺序） */
typedef struct {
    uint32_t magic;             /* CONFIG_STORE_MAGIC */
    uint16_t version;           /* 配置结构体版本 */
    uint16_t length;            /* 数据长度 */
    uint32_t sequence;          /* 递增序号，越大越新 */
    uint32_t crc32;             /* 记录头 + 数据 的 CRC32 */
    uint32_t reserved;          /* 保留（写擦除态） */
} config_store_hdr_t;

/* 底层 Flash 操作：地址为绝对地址；成功返回 0 */
typedef struct {
    int (*read)(uint32_t addr, void *buf, uint32_t size);
    int (*write)(uint32_t addr, const void *buf, uint32_t size);
    int (*erase)(uint32_t addr, uint32_t size);
} config_store_ops_t;

/* 存储布局（由调用者静态提供，描述两区位置、对齐与容量） */
typedef struct {
    uint32_t sector_addr[2];    /* 两个独立擦除区的起始地址 */
    uint32_t sector_size;       /* 每个区域大小（= 最小擦除单元） */
    uint32_t program_size;      /* 记录对齐粒度（通常 4；小于 4 按 4 处理） */
    uint32_t max_data_size;     /* 单条记录允许的最大数据长度 */
    uint16_t version;           /* 保存时写入的版本号 */
} config_store_layout_t;

/* 运行期状态（由 config_store_init 填充，之后只读使用） */
typedef struct {
    const config_store_layout_t *layout;
    const config_store_ops_t *ops;
    uint32_t active_index;      /* 当前追加的扇区下标：0/1 */
    uint32_t write_offset;      /* 当前扇区内的追加偏移 */
    uint32_t sequence;          /* 最新有效记录的序号 */
    uint32_t newest_addr;       /* 最新有效记录的绝对地址 */
    uint32_t record_count;      /* 当前扇区内的有效记录数（诊断用） */
    uint16_t version;           /* 最新有效记录的版本 */
    uint16_t length;            /* 最新有效记录的数据长度 */
    uint8_t  initialized;
    uint8_t  has_record;
} config_store_t;

/*
 * 初始化：保存布局/操作表，扫描两个扇区并定位最新有效记录。
 * 返回 CONFIG_STORE_OK 或错误码。
 */
int config_store_init(config_store_t *store,
                      const config_store_layout_t *layout,
                      const config_store_ops_t *ops);

/*
 * 加载配置到 data（长度必须与最新记录的 length 完全一致）。
 * 返回 CONFIG_STORE_OK / NONE / ERR_SIZE / ERR_VERIFY / ERR_IO。
 */
int config_store_load(config_store_t *store, void *data, uint32_t size);

/*
 * 原始加载：允许调用者按自己的缓冲长度取数据，并输出实际长度与版本，
 * 用于结构体升级时做版本迁移（数据按原样拷贝，不做解释）。
 * 返回 CONFIG_STORE_OK / NONE / ERR_SIZE（缓冲不足）/ ERR_VERIFY / ERR_IO。
 */
int config_store_load_raw(config_store_t *store, void *buf, uint32_t cap,
                          uint32_t *len, uint16_t *version);

/*
 * 保存一条新记录（追加写；当前扇区满时自动擦除另一区并轮转）。
 * 返回 CONFIG_STORE_OK 或错误码。
 */
int config_store_save(config_store_t *store, const void *data, uint32_t size);

/* 重新扫描两区并刷新最新记录信息；返回 OK（有记录）或 NONE */
int config_store_check(config_store_t *store);

/* 清空配置（擦除两个扇区），返回 OK 或 IO 错误 */
int config_store_format(config_store_t *store);

#ifdef __cplusplus
}
#endif

#endif /* CONFIG_H */
