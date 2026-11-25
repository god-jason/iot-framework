# KVFS - 双区滚动日志键值存储

面向 MCU 的**小数据键值存储**：`u32 key → 一段小数据`，用于保存配置、标定参数、
运行计数等场景。所有更新都是追加写，不原地覆盖；两个分区 A/B 轮流使用，
写满时把有效数据搬到另一区并擦除旧区。

> 不是通用文件系统：没有目录树、没有文件名、不支持任意长度的大文件。
> 需要文件/目录语义请用 `components/mfs`。

## 1. 设计来源

| 参考 | 借鉴点 |
|---|---|
| [boot_meta.c](../../platforms/stm32/boot_meta.c) | 分区状态记录 + CRC 校验 + 追加历史 |
| [config.c](../config/config.c) | 双区轮转、记录追加、**Commit 最后写** |

相比 `config`：从"只保存一份结构体"扩展到"多键独立更新"，并增加了
写满后的**有效数据搬迁**（相当于整分区 GC）。

## 2. 存储布局

```
介质分区（kvfs_blockdev_t.total_size）
┌──────────────────────────────┐  <- start_addr
│ 分区 A（region_size）         │
│   0x00: SUPER 32B（最后写入） │
│   0x20: 记录 1                │
│         记录 2 ...            │
│         0xFF ...（空尾）      │
├──────────────────────────────┤  <- start_addr + region_size
│ 分区 B（region_size）         │
│   与 A 结构完全相同           │
└──────────────────────────────┘
```

- `region_size = floor(total_size / 2 / erase_size) * erase_size`
- 要求 `total_size >= 2 * erase_size`，且 `region_size >= 512`
- 只有 `SUPER` 已提交的分区才可用；A/B 中取 `generation` 更大的一个

### 2.1 SUPER（32B，位于分区起始）

| 字段 | 长度 | 说明 |
|---|---|---|
| magic | 4 | `'K','V','F','S'`（"KVFS"） |
| version | 2 | `KVFS_VERSION` |
| hdr_size | 2 | 固定 32 |
| generation | 4 | 分区代次，越大越新 |
| data_start | 4 | 记录区起始（=32） |
| record_count | 4 | 提交时记录数 |
| live_bytes | 4 | 有效 value 字节数（诊断） |
| rsv | 4 | 保留 |
| crc32 | 4 | 前 28B 的 CRC32 |

SUPER 是分区的**提交点**：压缩时先复制全部有效记录，最后才写 SUPER。
掉电发生在写 SUPER 之前，该分区不会被挂载选中。

### 2.2 记录（Header 24B + Payload + 对齐 + Commit 4B）

| 字段 | 长度 | 说明 |
|---|---|---|
| magic | 4 | `'K','V','R','E'`（"KVRE"） |
| version | 2 | `KVFS_VERSION` |
| length | 2 | Payload 字节数（0 ~ max_value） |
| sequence | 4 | 全局递增序号，越大越新 |
| crc32 | 4 | Header（本字段按 0）+ Payload 的 CRC32 |
| key | 4 | 键 |
| flags | 1 | bit0 = 墓碑（删除） |
| rsv | 3 | 保留 |

```
+----------+---------+------------------+----------+
| Header   | Payload | Padding(0xFF)    | Commit   |
| 24B      | Len B   | 对齐到编程粒度    | 4B "KVCM"|
+----------+---------+------------------+----------+
```

写入顺序（单条记录掉电安全）：

1. 写 Header + Payload；
2. 回读 Header，并从介质重算 Payload CRC 复核；
3. 最后写 Commit 4B —— 没有 Commit 的记录一律忽略。

## 3. 读、写、删除与压缩

| 操作 | 行为 |
|---|---|
| `kvfs_put` | 追加一条记录（同 key 旧记录自动失效） |
| `kvfs_get` / `kvfs_size` | 扫描日志，取同 key 中 `sequence` 最大者 |
| `kvfs_del` | 追加一条墓碑记录（length = 0，flags bit0 = 1） |
| `kvfs_foreach` | 扫描出每个 key 的最新存活记录，逐条回调 |
| 写满 | 自动触发分区切换（压缩）；也可 `kvfs_gc` 手动触发 |

压缩（分区切换）流程：

```
1) 扫描活动区 -> 建立“每个 key 的最新记录”表（静态 32 槽）
2) 擦除另一分区（其内容只会更旧）
3) 第二遍扫描，把最新且未删除的记录按原序号复制到新分区
4) 写 SUPER（generation + 1）           <-- 新分区此刻才提交
5) 内存状态切到新分区
6) 擦除旧分区，供下一轮使用
```

任意一步掉电都能恢复到某一完整分区：新分区没有 SUPER 就继续用旧分区。

## 4. API 速览

初始化与生命周期：

```c
int kvfs_init(kvfs_t *kv, const kvfs_blockdev_t *dev, const kvfs_config_t *cfg);
int kvfs_format(kvfs_t *kv);      // 擦除两区并写 SUPER（代次 1）
int kvfs_mount(kvfs_t *kv);       // 选更大代次的已提交分区
int kvfs_unmount(kvfs_t *kv);
int kvfs_sync(kvfs_t *kv);
int kvfs_check(kvfs_t *kv);       // 全量校验 CRC（慢，建议诊断时调用）
int kvfs_statfs(kvfs_t *kv, kvfs_statfs_t *stat);
int kvfs_gc(kvfs_t *kv);          // 手动压缩
```

键值操作：

```c
int kvfs_put(kvfs_t *kv, uint32_t key, const void *value, uint32_t len);
int kvfs_get(kvfs_t *kv, uint32_t key, void *buf, uint32_t cap, uint32_t *len);
int kvfs_size(kvfs_t *kv, uint32_t key, uint32_t *len);
int kvfs_exists(kvfs_t *kv, uint32_t key);   // 1=存在，0=不存在，负值=错误
int kvfs_del(kvfs_t *kv, uint32_t key);
int kvfs_foreach(kvfs_t *kv, kvfs_visit_cb cb, void *ctx);
const char *kvfs_strerror(int error);
```

### 4.1 使用示例

```c
#include "kvfs.h"

#define KEY_WIFI_SSID   0x00010001u
#define KEY_WIFI_PASS   0x00010002u
#define KEY_BOOT_COUNT  0x00020001u

static uint8_t s_work[128];       /* 工作缓冲：>= cfg.max_value */
static kvfs_t  s_kv;

/* 由 app 适配层实现：内部 Flash 或 W25Q */
extern int board_flash_read(void *ctx, uint32_t addr, void *buf, uint32_t size);
extern int board_flash_program(void *ctx, uint32_t addr, const void *buf, uint32_t size);
extern int board_flash_erase(void *ctx, uint32_t addr, uint32_t size);

int config_store_init(void)
{
    kvfs_blockdev_t dev = {
        .read = board_flash_read,
        .program = board_flash_program,
        .erase = board_flash_erase,
        .sync = NULL,
        .ctx = NULL,
        .start_addr = 0x08060000u,     /* 分区起始（绝对地址） */
        .total_size = 32u * 1024u,     /* 两个 16KB 扇区 */
        .erase_size = 16u * 1024u,     /* F4 最小擦除单位 */
        .program_size = 4u,            /* F4 字编程 */
    };
    kvfs_config_t cfg = {
        .workbuf = s_work,
        .workbuf_size = sizeof(s_work),
        .max_value = 96u,              /* 单条数据上限 */
    };

    if (kvfs_init(&s_kv, &dev, &cfg) != KVFS_OK) {
        return -1;
    }
    if (kvfs_mount(&s_kv) == KVFS_ERR_FORMAT) {
        if (kvfs_format(&s_kv) != KVFS_OK) {
            return -1;
        }
    }
    return 0;
}

void save_wifi(const char *ssid, const char *pass)
{
    kvfs_put(&s_kv, KEY_WIFI_SSID, ssid, (uint32_t)strlen(ssid));
    kvfs_put(&s_kv, KEY_WIFI_PASS, pass, (uint32_t)strlen(pass));
}

int load_ssid(char *out, uint32_t cap)
{
    uint32_t len = 0;

    if (kvfs_get(&s_kv, KEY_WIFI_SSID, out, cap, &len) != KVFS_OK) {
        return -1;
    }
    out[len] = '\0';
    return (int)len;
}
```

遍历：

```c
static int dump_one(void *ctx, uint32_t key, const void *value, uint32_t len)
{
    printf("key=0x%08X len=%u\n", key, len);
    return 0;             /* 非 0 可提前结束遍历 */
}

kvfs_foreach(&s_kv, dump_one, NULL);
```

## 5. 约束与注意事项

1. **键数量上限**：压缩需要静态槽位表，默认 `KVFS_MAX_KEYS = 32`
   （可在编译选项或头文件覆盖）。超过后写入仍可继续，但压缩返回
   `KVFS_ERR_NOSPC`；删除部分键后可恢复。
2. **单条长度上限**：`KVFS_VALUE_MAX = 1024`，实际由 `cfg.max_value` 收紧；
   `cfg.workbuf` 必须不小于 `max_value`。
3. **`kvfs_get` 的 `buf` 可以为 NULL**：只查询存在性；`cap` 不足返回
   `KVFS_ERR_NOMEM`，但 `*len` 仍会写入实际长度。
4. **`kvfs_foreach` 回调期间持有实例锁**，不要在回调里调用其它 `kvfs_*`
   （避免非递归锁死锁）；需要写回时先收集到本地数组，回调结束后再操作。
5. **容量语义**：`kvfs_statfs` 的 `used_bytes` 是活动区已追加字节数，
   包含尚未压缩的旧记录；`free_bytes` 到 0 前会自动压缩。
6. **只读模式**：`cfg.readonly = 1` 时禁止 put/del/format/gc，只允许读。
7. **`kvfs_check` 会读取整个分区数据**（校验每条记录 CRC），
   上电挂载只做结构与 Commit 快速扫描，不做全量 CRC。
8. **键建议用宏/枚举**（如 `0x0001xxxx` 按模块分段），不要用字符串哈希，
   避免升级后 key 冲突。
9. **魔术字**（定义见 `kvfs_internal.h` 的 `KVFS_MAGIC4`）：
   `KVFS`（分区 SUPER）、`KVRE`（记录头）、`KVCM`（提交标记）。
   格式不兼容时会返回 `KVFS_ERR_FORMAT`（按未格式化处理）。

## 6. 资源占用

ARM Cortex-M4（`-Os`）实测：

| 项目 | 大小 |
|---|---|
| 代码（kvfs.c + kvfs_log.c + kvfs_store.c） | 约 3.9 KB Flash |
| 键槽表（`KVFS_MAX_KEYS` = 32） | 640 B `.bss`（组件内静态） |
| `kvfs_t` 实例 | 160 B（调用者静态分配） |
| 工作缓冲 | `>= cfg.max_value`（调用者提供） |
| 栈占用 | 无递归、无大局部数组（最大局部缓冲为 32B 的 CRC 分块） |

## 7. 接入真实 Flash

只需实现 `kvfs_blockdev_t` 的三个回调：

| 回调 | 要求 |
|---|---|
| `read` | 任意地址/长度读取，返回 0 成功 |
| `program` | 只能 1→0；目标必须已擦除；可跨页由驱动自行处理 |
| `erase` | `addr`/`size` 必须按 `erase_size` 对齐 |

参数建议：

| 介质 | `erase_size` | `program_size` |
|---|---|---|
| STM32F4 内部 Flash | 16 KB（最小扇区） | 4 |
| STM32F1 内部 Flash | 1 KB | 4 |
| W25Qxx（SPI NOR） | 4 KB | 1 |

内部 Flash 注意：F4 扇区最小 16KB，两区需要 32KB；且执行 Flash 擦写时
代码不能运行在同一 Bank 上（按芯片手册处理）。

## 8. 测试

组件自带主机测试 [kvfs_test.c](kvfs_test.c)，用 RAM 模拟 Flash
（编程只能 1→0、按扇区擦除），覆盖：

- 未格式化挂载
- 格式化、读写、覆盖、缓冲不足、超长拒绝
- 删除与墓碑
- 掉电半条记录（Header 已写、Commit 未写）
- 写满多轮滚动切换 + 数据校验
- 手动压缩、遍历
- 只读挂载

```bash
xmake build ros-test && xmake run ros-test
```

也可直接用主机编译器单独做语法检查（组件不依赖平台层）：

```bash
gcc -std=c99 -fsyntax-only components/kvfs/kvfs*.c -Icomponents/kvfs -Icomponents/common
```
