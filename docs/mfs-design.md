# MFS 设计文档

版本：V1.1（MAJOR=1, MINOR=1）
适用范围：`src/mfs`
最后更新：2026-11-24

---

## 1. 概述

MFS（Record-Oriented File System）是一个面向 SPI NOR / 内部 Flash 的轻量级
日志结构文件系统，目标平台为 32 位 MCU。核心特性：

- 追加写（Log-Structured）：所有更新生成新 Record，不原地覆盖；
- Record 级 CRC32 + 独立提交标记（Commit Marker），掉电只丢失尾部未提交记录；
- Inode 统一描述文件与目录，文件数据按 256B 逻辑 Chunk 存储；
- 静态 RAM，不使用 malloc，句柄数量编译期固定；
- Block GC、动态磨损均衡、坏块管理（V1.1）；
- 与 MCU/块设备解耦，可运行在 SPI NOR、内部 Flash 或 PC 上的镜像文件。

非目标：

- 不提供 POSIX 完整语义（无硬链接、权限、时间精度仅到秒）；
- V1 不实现多记录事务与 Checkpoint（接口保留，返回 `MFS_ERR_NOTSUP`）；
- 不维护常驻目录树/元数据缓存，查询采用按需全盘扫描，适合小容量分区。

---

## 2. 代码结构

| 文件 | 职责 |
| --- | --- |
| `mfs.h` | 公共 API、错误码、配置/句柄结构、物理格式说明 |
| `mfs_internal.h` | 内部物理格式常量、序列化辅助、模块间接口 |
| `mfs.c` | 生命周期：init / format / mount / unmount / sync / check，Superblock 读写，空间统计 |
| `mfs_blockdev.c` | 块设备回调封装（read / program / erase / sync）与错误映射 |
| `mfs_block.c` | Block Header 读写、块分配（磨损均衡）、空闲判定、坏块标记 |
| `mfs_record.c` | Record 写入/读取/CRC 校验、块扫描、DATA 定位 |
| [crc32.c](../src/crypto/crc32.c) | CRC32（src/crypto 提供，record/block/sb 共用） |
| `mfs_inode.c` | Inode Payload 序列化/解析、最新版本查找、目录项查找、创建/更新/删除 |
| `mfs_dir.c` | 路径解析、mkdir/rmdir/unlink/rename/stat、目录遍历 |
| `mfs_file.c` | 文件打开/读写/seek/truncate，Chunk 读写策略 |
| `mfs_gc.c` | 有效记录判定、牺牲块选择、有效记录复制、GC 主流程 |
| `mfs_maint.c` | GC 公开接口、磨损统计、坏块标记、事务占位、错误字符串 |
| `mfs_test.c` | RAM 块设备上的 20 组单元测试（仅 host `ros-test`） |

依赖方向：

```
mfs.h (public)
   ^
   |-- mfs_internal.h
         |
         +-- mfs.c ----------- mfs_blockdev.c
         |      |                    ^
         |      +-- mfs_block.c ----+
         |      +-- mfs_record.c ---+
         |      +-- mfs_inode.c
         |      +-- mfs_dir.c
         |      +-- mfs_file.c
         |      +-- mfs_gc.c
         |      +-- mfs_maint.c
         +-- （CRC32 由 src/crypto/crc32.c 提供）
```

---

## 3. 介质布局

### 3.1 分区布局（以 4KB 扇区为例）

```
偏移 0            +----------------------+  <- dev.start_addr
                  |  Superblock A (4KB)  |  仅使用前 24B
                  +----------------------+
                  |  Superblock B (4KB)  |  仅使用前 24B
                  +----------------------+
                  |  Pool Block 0        |  Block Header(24B) + Record 日志
                  +----------------------+
                  |  Pool Block 1        |
                  +----------------------+
                  |        ...           |
                  +----------------------+
```

- 池内块号 `b` 的绝对地址：`start_addr + (b + 2) * block_size`；
- `block_size = dev.erase_size`，最小 256B，分区至少 3 个块；
- 前两块固定留给 Superblock A/B，不参与数据分配。

### 3.2 Superblock（24B，小端）

| 偏移 | 长度 | 字段 | 说明 |
| --- | --- | --- | --- |
| +0 | 4 | magic | `"MFS"` (0x53464F52) |
| +4 | 4 | version | `(MAJOR << 16) | MINOR` |
| +8 | 4 | block_size | 必须等于 `dev.erase_size` |
| +12 | 4 | block_count | 数据池块数（不含 A/B） |
| +16 | 4 | generation | 格式化代次，每次 format 递增 |
| +20 | 4 | crc | 前 20B 的 CRC32 |

A/B 内容相同，挂载时选择 valid 且 generation 更大的一份；两份都无效返回
`MFS_ERR_FORMAT`。

### 3.3 Block Header（24B，小端）

| 偏移 | 长度 | 字段 | 说明 |
| --- | --- | --- | --- |
| +0 | 4 | magic | `"!BLK"` (0x4B4C4221) |
| +4 | 4 | sequence | 擦除/分配顺序，越大越新 |
| +8 | 4 | erase_count | 累计擦除次数（磨损均衡） |
| +12 | 4 | state | 1=FREE，2=BAD |
| +16 | 4 | crc | 前 16B 的 CRC32 |
| +20 | 4 | reserved | 写 0 |

CRC 只覆盖前 16B（不含 crc 自身与 reserved），避免自校验污染。

### 3.4 Record

```
+0   Record Header (32B)
      +0  magic        4B  "REC1"
      +4  sequence     4B  全局单调递增
      +8  tx_id        4B  事务号（V1 每次操作一个）
      +12 object_id    4B  Inode 编号
      +16 type         2B  MFS_REC_*
      +18 flags        2B  保留
      +20 payload_size 4B
      +24 payload_crc  4B
      +28 header_crc   4B  前 28B 的 CRC32
+32  Payload
+X   Padding（保持 0xFF）
+M   Commit Marker（4B，"CMIT"）

M = align_up(record_start + 32 + payload_size, align)
align = max(program_size, 4)，上限 256
```

写入顺序：Header → Payload(s) → sync → Commit Marker → sync。
扫描时 magic / header_crc / marker / payload_crc 任一不通过即视为块尾结束，
因此掉电只会丢失尾部未提交记录。

Record 类型：

| 值 | 类型 | 用途 |
| --- | --- | --- |
| 1 | INODE_CREATE | 创建文件/目录 |
| 2 | INODE_UPDATE | 更新大小、名称、父目录、epoch |
| 3 | INODE_DELETE | 删除标记（payload: inode + version） |
| 4 | DATA | 文件 Chunk 数据 |
| 5 | DATA_DELETE | 批量失效一段 Chunk（截断） |
| 6-8 | TX_COMMIT / CHECKPOINT / GC_MOVE | 预留 |

### 3.5 INODE Payload（32B 前缀 + 名称）

| 偏移 | 长度 | 字段 |
| --- | --- | --- |
| +0 | 4 | inode |
| +4 | 4 | parent |
| +8 | 4 | size（目录为 0） |
| +12 | 4 | version（单调递增） |
| +16 | 4 | create_time |
| +20 | 4 | modify_time |
| +24 | 4 | epoch（数据代次） |
| +28 | 2 | name_len（<= 31） |
| +30 | 1 | type（1=文件，2=目录） |
| +31 | 1 | flags |
| +32 | N | name（不含 '\0'） |

### 3.6 DATA Payload（24B 前缀 + 数据）

| 偏移 | 长度 | 字段 |
| --- | --- | --- |
| +0 | 4 | inode |
| +4 | 4 | chunk |
| +8 | 4 | generation（写入时的全局 seq） |
| +12 | 4 | epoch |
| +16 | 2 | data_size（<= 256） |
| +18 | 2 | flags |
| +20 | 4 | reserved |
| +24 | N | data |

### 3.7 DATA_DELETE Payload（16B）

| 偏移 | 长度 | 字段 |
| --- | --- | --- |
| +0 | 4 | inode |
| +4 | 4 | start_chunk |
| +8 | 4 | count |
| +12 | 4 | epoch（仅失效同 epoch 数据） |

---

## 4. 生命周期

### 4.1 状态

```
未初始化 --mfs_init--> 已初始化 --mfs_mount--> 已挂载
                             |                     |
                             +--mfs_format------> 已挂载（格式化即挂载）
已挂载 --mfs_unmount--> 已初始化
```

`mfs_init` 只做参数校验与内存初始化，不访问 Flash。

### 4.2 格式化流程

1. 尝试读取旧 Superblock，generation 递增（读不到则从 1 开始）；
2. 复位运行期状态；
3. 逐块处理：
   - 已是 BAD 的块保留，`bad_blocks++`；
   - 擦除成功 → 写 FREE 头（erase_count 旧值 + 1），`free_blocks++`；
   - 擦除失败 → 尽量写 BAD 头，`bad_blocks++` 后继续；
4. 擦除 Superblock A/B；
5. 按磨损均衡分配初始活动块；
6. 创建根目录 Inode（编号固定为 1）；
7. 写 A/B 两份 Superblock（写完并 sync 才算格式化完成）。

### 4.3 挂载流程

1. 读取并校验 A/B，选择 generation 更大的有效副本；
2. 逐块读取 Block Header：
   - Header 无效 → 视为空闲块，`free_blocks++`；
   - state=BAD → `bad_blocks++`，跳过；
   - 扫描记录，恢复全局 `seq`、`next_inode`、`next_tx`、块尾偏移；
   - 无记录且首 4B 为 0xFF → 空闲块；否则视为“写坏的垃圾块”，留给 GC；
3. 选择 sequence 最大且仍有空间的块作为活动块；
4. 校验根 Inode 存在且为目录，否则返回 `MFS_ERR_FORMAT/CORRUPT`。

挂载不依赖任何旧 RAM 状态，可重复调用；挂载时清空残留句柄。

---

## 5. 写入路径

`mfs_rec_write(fs, type, object_id, p1, l1, p2, l2)`：

1. 无活动块时调用 `mfs_blk_alloc`；
   - 无可用块且 `cfg.auto_gc != 0` 且当前不在 GC 中 → 先强制 GC 一次再重试；
2. 计算 `marker_off = align_up(active_off + 32 + payload_size, align)`；
   - 若 `marker_off + 4 > block_size`：放弃当前块尾部，分配新块；
   - 单条记录超过整块容量 → `MFS_ERR_NOSPC`；
3. 分两段计算 Payload CRC（DATA = 前缀 + 数据，无需拼接大缓冲）；
4. 写 Header、Payload、sync；
5. 写 Commit Marker、sync；
6. 更新 `active_off`、`used_bytes`、`seq`。

任一步失败都不会写 Marker，挂载后该记录自动被忽略。

---

## 6. 文件数据模型

- 文件大小由最新 INODE 记录的 `size` 决定；
- `chunk = offset / 256`，同一 Chunk 的更新生成更大的 `generation`；
- 读取时取该 `(inode, chunk, epoch)` generation 最大的 DATA，
  若存在 sequence 更大的 DATA_DELETE 覆盖则视为空洞并补 0；
- 写入策略：
  - 整 Chunk 对齐写：直接追加 DATA；
  - 非对齐/部分写：读出旧 Chunk 到 workbuf，合并后整块写；
  - 所有 Chunk 写完且文件变大时追加 INODE_UPDATE。
- 截断（仅支持缩小）：
  1. 边界 Chunk 改写为缩短后的 DATA（防止扩展时暴露旧字节）；
  2. 后续完整 Chunk 写一条 DATA_DELETE；
  3. 更新 Inode.size。

`mfs_data_locate` 统一了“最新 DATA + DATA_DELETE”的判定，
文件读取与 GC 有效记录判定共用同一套语义，避免两处逻辑漂移。

---

## 7. 目录模型

- 目录也是 Inode；子项通过 `(parent, name)` 关联，不维护常驻目录树；
- 路径必须以 `/` 开头，禁止空分量、`.`、`..` 和结尾 `/`；
- `mfs_inode_lookup(parent, name)`：
  1. 扫描所有 INODE_CREATE/UPDATE 记录，按 `(parent, name)` 收集候选 Inode；
  2. 对每个候选调用 `mfs_inode_find` 取最新版本；
  3. 最新版本仍匹配 `(parent, name)` 才命中，防止改名后的历史记录干扰；
- `mfs_readdir` 按日志 sequence 升序逐条返回：
  - 只考虑 sequence > `iter_seq`；
  - 只返回该 Inode 的最新记录（避免 CREATE+UPDATE 重复枚举）；
  - 选择 sequence 最小的候选，保证遍历稳定。

---

## 8. GC 设计

### 8.1 有效记录判定

| 记录类型 | 保留条件 |
| --- | --- |
| INODE_CREATE/UPDATE | 是该 Inode 最新记录且未被删除 |
| INODE_DELETE | 该 Inode 最新记录为删除 |
| DATA | 是该 `(inode, chunk, epoch)` 最新 DATA，未被 DATA_DELETE 覆盖，且 chunk 在文件大小范围内 |
| DATA_DELETE | Inode 仍存在且 epoch 一致 |
| 未知/预留 | 保守保留 |

### 8.2 牺牲块选择

- 跳过活动块、正在 GC 的块、BAD 块、空闲块；
- 统计块内有效字节数 `live`，`ratio = live * 100 / usable`；
- 默认只回收 `ratio < 75%` 的块（`force=1` 时允许任意比例）；
- 相同比例时优先选择 `erase_count` 更高的块（磨损均衡）；
- 无候选时：普通 GC 返回 OK，强制 GC 返回 `MFS_ERR_NOSPC`。

### 8.3 执行顺序（先复制、后擦除）

1. 设置 `gc_running=1`、`gc_block=victim`；
2. 扫描牺牲块，把有效记录逐条复制到当前日志（每条独立提交）；
3. sync；
4. 擦除牺牲块，写 FREE 头（erase_count + 1）；
5. 更新 `free_blocks` / `used_bytes`，清除 GC 标志。

掉电安全性：若在 2/3 之后、4 之前掉电，新旧记录同时存在，
读取按 generation/sequence 选择新副本；下次 GC 会发现旧块已无有效记录并直接回收。

约束与错误：

- 存在打开目录句柄时返回 `MFS_ERR_BUSY`（避免 readdir 重复枚举）；
- `gc_running` 防止写路径递归触发 GC；
- 牺牲块擦除失败时标记 BAD，数据已复制，文件系统仍可用。

---

## 9. 磨损均衡

- 每个 Block Header 持久化 `erase_count`；
- 分配时从 `alloc_hint` 开始轮转扫描，选择 `erase_count` 最小的空闲块；
- 同擦除次数时按轮转顺序取第一个，避免总是磨损低编号块；
- 未初始化块按 0 处理，擦除后写 FREE 头并置 1；
- GC 擦除牺牲块时 `erase_count + 1`，使高磨损块在后续分配中自然被跳过；
- `mfs_erase_stat` 输出全盘 min/max/avg，便于评估磨损分布。

---

## 10. 坏块管理

### 10.1 标记来源

1. 擦除失败自动标记：format / 块分配 / GC 擦除失败时写 BAD 头；
2. 手动标记：`mfs_mark_bad(fs, block)`，会擦除该块（数据丢失）。

### 10.2 持久化与跳过

- BAD 状态写入 Block Header（state=2）并带 CRC；
- mount 统计 `bad_blocks`，format 保留 BAD（不会复活）；
- `mfs_blk_alloc`、GC 选块、`mfs_erase_stat` 全部跳过 BAD；
- 已标记的块再次标记返回 `MFS_ERR_INVAL`，避免重复计数；
- 活动块、正在 GC 的块、越界块不允许标记。

### 10.3 空间统计

```
total_bytes = block_count * block_size
free_bytes  = free_blocks * block_size + 活动块尾部剩余
used_bytes  = total_bytes - free_bytes - bad_blocks * block_size
```

`free_blocks` 只统计完全空闲块；部分使用的旧块由 GC 回收后才重新计入。

---

## 11. 并发与锁

- 若 `cfg.lock/unlock` 非空，所有公开 API 进入时加锁、退出时解锁；
- 块设备回调本身不重复加锁，避免非递归互斥量死锁；
- 内部函数（`mfs_*`）假定调用者已持有锁；
- GC 与写路径共享 `gc_running` / `gc_block` 状态，禁止重入。

---

## 12. 公共 API 摘要

| 分类 | 接口 |
| --- | --- |
| 生命周期 | `mfs_init` / `mfs_format` / `mfs_mount` / `mfs_unmount` / `mfs_sync` |
| 空间 | `mfs_statfs` / `mfs_free_space` / `mfs_total_space` |
| 文件 | `mfs_open` / `mfs_close` / `mfs_read` / `mfs_write` / `mfs_seek` / `mfs_tell` / `mfs_size` / `mfs_truncate` / `mfs_fsync` |
| 目录 | `mfs_mkdir` / `mfs_rmdir` / `mfs_unlink` / `mfs_rename` / `mfs_exists` / `mfs_stat` / `mfs_isdir` / `mfs_opendir` / `mfs_readdir` / `mfs_closedir` |
| 维护 | `mfs_gc` / `mfs_gc_needed` / `mfs_mark_bad` / `mfs_erase_stat` / `mfs_check` |
| 预留 | `mfs_tx_begin/commit/abort` / `mfs_checkpoint`（均返回 NOTSUP） |
| 工具 | `mfs_strerror` |

所有接口成功返回 0；读写返回字节数；失败返回负 `MFS_ERR_*`。
`mfs_readdir` 使用 0=有项、1=结束、负值=错误。

---

## 13. RAM / Flash 预算（f407ve 实测）

测试条件：`-p cross --stm32_family=f4 --stm32_chip=STM32F407xx`，
`release`、`-Os` 默认优化、`-Wl,--gc-sections`，APP FLASH 区 128KB。

### 13.1 整体镜像

| 项目 | 重构前 | 重构后 | 变化 |
| --- | --- | --- | --- |
| text | 101,276 | 100,992 | -284 |
| data | 580 | 580 | 0 |
| bss | 49,912 | 49,912 | 0 |
| Flash 合计（text+data） | 101,856 | 101,572 | -284 |
| Flash 占用率（128KB） | 77.7% | 77.5% | -0.2pp |

Flash 剩余约 29.5KB（131,072 - 101,572）。

### 13.2 MFS 代码占用（链接后实占，仅统计 FLASH 地址区间，排除调试段）

| 对象 | 字节 | 说明 |
| --- | --- | --- |
| mfs.c | 1,241 | 生命周期、SB、mount 扫描、statfs |
| mfs_blockdev.c | 132 | 块设备封装 |
| mfs_block.c | 650 | 块头、分配、坏块 |
| mfs_record.c | 1,558 | 记录读写、扫描、DATA 定位 |
| mfs_inode.c | 1,260 | Inode 解析/查找 |
| mfs_dir.c | 1,528 | 路径与目录 |
| mfs_file.c | 1,386 | 文件读写 |
| mfs_gc.c | 1,006 | GC 判定与复制 |
| mfs_maint.c | 399 | GC 接口、坏块标记、strerror |
| **合计** | **9,218** | 约占镜像 9.1%，占 128KB Flash 7.0% |

应用层 `apps/f407ve/app_fs.c`（SPI 初始化 + MFS 挂载 + 8 条 shell 命令）
链接后 2,487B。MFS 功能总成本约 11.7KB（8.9% Flash）。

### 13.3 MFS RAM

| 项目 | 字节 |
| --- | --- |
| `s_mfs`（mfs_t，含句柄池） | 308 |
| `s_mfs_work`（工作缓冲） | 256 |
| **合计** | **564** |

符合“核心 RAM <= 1KB”的目标；句柄数量由 `MFS_MAX_OPEN_FILES=2`、
`MFS_MAX_OPEN_DIRS=1` 编译期确定。

### 13.4 镜像其他主要占用（参考）

| 对象 | 字节 |
| --- | --- |
| roshell_builtin.c | 5,175 |
| roshell_eval.c | 4,687 |
| device.c | 2,688 |
| roshell_interp.c | 2,623 |
| app_fs.c | 2,487 |
| app_ml307.c | 2,256 |

---

## 14. 测试矩阵

`src/mfs/mfs_test.c` 使用 64KB RAM 块设备（16 x 4KB，NOR 1→0，
支持擦除故障注入）覆盖以下 20 组：

| 用例 | 覆盖点 |
| --- | --- |
| mfs/format-mount | 格式化、根目录、mkdir、重复 mkdir |
| mfs/file-write-read | 创建、写、tell/size、seek、读回 |
| mfs/file-readonly-seek | 只读打开、SEEK_END |
| mfs/append-random-truncate | 追加、覆盖、截断 |
| mfs/multi-chunk | 600B 跨 3 个 Chunk |
| mfs/truncate-hole | 截断后扩展的空洞为 0 |
| mfs/rename-stat | 重命名、stat |
| mfs/readdir | 目录遍历 |
| mfs/readdir-update | CREATE+UPDATE 去重 |
| mfs/statfs | 空间统计 |
| mfs/remount | 卸载重挂载持久化 |
| mfs/remove | unlink、rmdir、非空目录保护 |
| mfs/gc-reclaim | 全垃圾块回收、数据保持 |
| mfs/wear-level | 擦除次数分布、跳过高磨损块 |
| mfs/gc-live-copy | 混合有效/失效记录的复制正确性 |
| mfs/gc-threshold | 有效比例 >= 75% 不回收 |
| mfs/gc-busy | 目录句柄打开时 GC 返回 BUSY |
| mfs/bad-block | 标记/重复标记/越界、容量统计、重挂载与格式化保留、分配跳过 |
| mfs/bad-skip-gc | GC 不选中/不擦除坏块 |
| mfs/bad-erase-fail | 格式化擦除失败自动标坏并继续 |

回归结果以 `xmake r ros-test` / CI 实际输出为准。

---

## 15. 已知限制与后续方向

1. 多记录事务未实现，跨多条记录的操作掉电后可能只完成一部分；
2. 无 Checkpoint，挂载需扫描全盘，容量大时挂载时间线性增长；
3. 无元数据缓存，路径解析与 readdir 为 O(N) 全盘扫描；
4. 路径组件上限 31B、路径上限 127B，不支持符号链接；
5. 单条 DATA 记录只能容纳 256B，随机写会产生较多失效记录，依赖 GC 回收；
6. 坏块标记采用“写 BAD 头”，若目标块未成功擦除，头部可能无法持久化，
   调用方应结合设备自身的坏块表使用；
7. GC 每次只回收一个块，持续高负载写入建议由应用周期性调用 `mfs_gc`，
   或开启 `auto_gc` 在空间耗尽时自动回收。

---

## 16. 变更记录

| 版本 | 内容 |
| --- | --- |
| V1.0 | 日志结构核心、Inode/Chunk 模型、掉电安全记录、目录与文件 API |
| V1.1 | Block GC、动态磨损均衡、坏块管理；Block Header 扩展为 24B 并修正 CRC 覆盖范围（16B） |
| V1.1-refactor | 拆分块设备层与维护层，统一 Inode 解析与 DATA 定位，路径解析缓存长度；镜像 -284B |
