# ROFS 1.0 只读文件系统设计文档

版本：ROFS 1.0（格式版本 1）
代码目录：`components/rofs`
打包工具：`tools/make_rofs.py`
最后更新：2026-11-24

---

## 1. 概述

ROFS 是面向 MCU 的**只读静态资源文件系统**：

- 镜像由 PC 端 Python 工具生成，MCU 端只负责挂载、查找、读取和目录遍历；
- 不使用 FAT / B+Tree / 索引表，不建立独立目录树；
- 不使用动态内存，文件与目录句柄由调用者静态分配；
- 不支持创建、修改、删除，不需要日志、GC 与空间整理；
- 元数据与文件数据连续存储，通过顺序扫描定位文件；
- 支持内部 Flash、SPI NOR、QSPI Flash、ROM 以及任何可按地址读取的介质。

典型用途：网页资源、Lua 脚本、JSON 配置、字体/图片、机器人动作脚本、
设备资源包等随固件发布、运行期只读的内容。

### 1.1 设计目标

| 项目 | 目标 |
| --- | --- |
| 语言 | C99 |
| 运行环境 | NoOS / RTOS |
| RAM | 核心对象 + 句柄控制在 1KB 以内 |
| 动态内存 | 不使用 malloc |
| 文件数量 | 由镜像大小决定 |
| 文件大小 | 单条记录最大 4GB（格式上限） |
| 文件定位 | 顺序扫描 |
| 文件读取 | 支持随机 seek |
| 目录 | 由路径前缀隐式表达，支持多级 |
| 校验 | 单文件 Body CRC32 + 整镜像 CRC32 |
| 镜像生成 | Python 3（仅标准库） |
| 字节序 | 固定小端，逐字节解析 |

### 1.2 非目标

- 不提供写接口、事务、磨损均衡、坏块管理；
- 不支持硬链接、权限、符号链接；
- 不支持运行时扩容或镜像内更新（更新需重新烧写镜像）；
- 第一版不实现索引表、目录树和缓存。

---

## 2. 代码结构

| 文件 | 职责 |
| --- | --- |
| [rofs.h](../components/rofs/rofs.h) | 公共 API、错误码、镜像常量、实例/句柄结构 |
| [rofs_internal.h](../components/rofs/rofs_internal.h) | 字段偏移、小端解析、内部记录接口 |
| [rofs.c](../components/rofs/rofs.c) | 挂载、卸载、镜像信息、完整校验 |
| [rofs_file.c](../components/rofs/rofs_file.c) | 路径校验、顺序查找、open/read/seek/tell/size/eof/exists/stat |
| [rofs_dir.c](../components/rofs/rofs_dir.c) | 基于路径前缀的 opendir/readdir/rewinddir/closedir |
| [crc32.c](../components/crypto/crc32.c) | CRC-32/ISO-HDLC（与 `zlib.crc32` 一致） |
| [rofs_test.c](../components/rofs/rofs_test.c) | RAM 镜像单元测试（仅 host `ros-test`） |
| [make_rofs.py](../tools/make_rofs.py) | 目录打包、镜像查看、提取与校验 |

模块关系：

```
rofs.h
  └── rofs_internal.h
        ├── rofs.c        （挂载/校验）
        ├── rofs_file.c   （查找/文件）
        └── rofs_dir.c    （目录）
```

---

## 3. 镜像格式

### 3.1 总体布局

```
+---------------------------+
| Super Header (32B)        |
+---------------------------+
| Record 1                  |
|   Header(16B)             |
|   Name(name_len)          |
|   Body(size)              |
+---------------------------+
| Record 2 ...              |
+---------------------------+
| END! (4B)                 |
+---------------------------+
```

- 所有记录连续排列，无对齐填充、无空闲区；
- 下一条记录偏移完全由当前记录的 `name_len` 与 `size` 推导：

```
next = record_offset + 16 + name_len + size
```

- 镜像末尾约束：

```
end_offset = records_offset + records_size
image_size = end_offset + 4
```

### 3.2 Super Header（32B，小端）

| 偏移 | 长度 | 字段 | 说明 |
| --- | --- | --- | --- |
| +0 | 4 | magic | `0x53464F52`（"ROFS"） |
| +4 | 2 | version | 当前为 1 |
| +6 | 2 | header_size | 固定 32 |
| +8 | 4 | image_size | 镜像总大小 |
| +12 | 4 | record_count | 记录数量 |
| +16 | 4 | records_offset | 第一条记录偏移（通常 32） |
| +20 | 4 | records_size | 记录区大小（不含 END） |
| +24 | 4 | image_crc32 | 整镜像 CRC32 |
| +28 | 4 | flags | 镜像标志，当前为 0 |

### 3.3 Record Header（16B，小端）

| 偏移 | 长度 | 字段 | 说明 |
| --- | --- | --- | --- |
| +0 | 1 | type | 1=FILE，2=DIR（保留） |
| +1 | 1 | name_len | 文件名长度，1~255 |
| +2 | 2 | flags | 文件属性，当前为 0 |
| +4 | 4 | size | Body 大小 |
| +8 | 4 | time | Unix 时间戳（可写 0） |
| +12 | 4 | crc32 | Body 的 CRC32 |

### 3.4 Name

- 相对路径，使用 `/` 分隔，例如 `config/device.json`；
- 不以 `/` 开头或结尾，不允许空段、`.`、`..`；
- UTF-8 编码，最长 255 字节，不带 `\0`；
- 不需要单独目录节点，目录由路径前缀隐式表达。

### 3.5 Body

紧随 Name 存放文件实际数据，长度由 `size` 指定，无填充。
`body_offset = record_offset + 16 + name_len`。

### 3.6 End 标记

4 字节 `"END!"`（小端数值 `0x21444E45`），位于记录区末尾，
不计入 `record_count`，用于挂载时快速验证镜像结构。

### 3.7 CRC 规则

采用 CRC-32/ISO-HDLC（反射多项式 `0xEDB88320`，初值/末值异或
`0xFFFFFFFF`），与 Python `zlib.crc32` 一致。

| 校验对象 | 计算范围 | 何时校验 |
| --- | --- | --- |
| Record CRC | 仅 Body（不含 Header/Name） | `rofs_check()` |
| Image CRC | 整个镜像（含 Super/END），`image_crc32` 字段按 0 计算 | `rofs_check()` |

Builder 生成镜像时完成全部 CRC；MCU 端按需调用 `rofs_check()` 复算。

---

## 4. 镜像打包工具

### 4.1 命令

```bash
python tools/make_rofs.py build   <输入目录> <输出.bin> [--zero-time]
python tools/make_rofs.py info    <镜像.bin>
python tools/make_rofs.py list    <镜像.bin>
python tools/make_rofs.py extract <镜像.bin> <路径> [输出文件]
python tools/make_rofs.py verify  <镜像.bin>
```

### 4.2 build 流程

1. 递归扫描输入目录，生成 `/` 分隔的相对路径；
2. 校验路径（首尾 `/`、空段、`.`、`..`、长度、重复）；
3. 读取文件内容，计算 Body CRC32；
4. 按 UTF-8 路径字节序排序（保证目录项连续、便于去重）；
5. 生成 Record（Header + Name + Body）；
6. 生成 Super Header 与 END 标记；
7. 回填 `image_size` 与整镜像 CRC（字段先置 0）；
8. 输出 `.bin`。

`--zero-time` 将时间字段写 0，便于可重复构建与镜像比对。

### 4.3 verify 检查项

- magic / version / header_size；
- image_size 与实际文件长度一致；
- records_offset / records_size 范围合法；
- `records_offset + records_size + 4 == image_size`；
- END 标记正确；
- 每条记录头、名称、Body 边界合法；
- 每条记录 Body CRC；
- 记录总长与 records_size 一致；
- 整镜像 CRC（字段按 0）。

### 4.4 输出示例

```
build ok: resource.bin
  files       : 4
  records size: 193 bytes
  image size  : 229 bytes (0.2 KB)
  image crc32 : 0x5B18E70C
```

---

## 5. 运行时设计

### 5.1 挂载

`rofs_mount(fs, base_offset, read, ctx)` 只读取 Super Header 与 END，
**不扫描记录**，因此挂载时间与镜像大小无关。

```
rofs_mount
  ├── 读取 32B Super Header
  ├── magic / version / header_size
  ├── image_size、records_offset、records_size 范围检查
  ├── image_size == records_offset + records_size + 4
  ├── 读取 records_end 处的 4B END
  └── 保存字段，mounted = 1
```

失败直接返回 `ROFS_ERR_MAGIC/VERSION/FORMAT/RANGE/IO`，不保留半挂载状态。

### 5.2 完整性校验

`rofs_check(fs)` 是可选的重操作，适用于烧写后自检或故障排查：

1. 逐条记录检查头/名称/Body 边界与 Body CRC；
2. 校验记录总长恰好等于 records_size；
3. 校验整镜像 CRC（`image_crc32` 字段按 0）。

分块读取（64B），不会为镜像分配大缓冲。

### 5.3 文件查找

`rofs__lookup()` 顺序扫描：

```
off = records_offset
for i in 0..record_count-1:
    读取并校验记录边界
    若 name_len == 路径长度:
        读取文件名并比较
        命中 -> 返回记录头与 Body 偏移
    off = Body 偏移 + size
```

- 只有长度相同时才读取文件名，减少不必要的介质访问；
- 名称比较区分大小写；
- 平均/最坏复杂度均为 O(n)，适合小容量资源镜像。

### 5.4 文件读取

`rofs_open()` 成功后句柄只保存：

```
offset   = Body 在镜像内的偏移
size     = 文件大小
position = 当前读取位置
```

`rofs_read()` 每次读取都会：

- 按 `size - position` 裁剪请求长度；
- 通过 `rofs__read()` 检查不越过镜像范围；
- 更新 `position`。

`rofs_seek()` 使用 64 位中间值计算，目标限制在 `[0, size]`；
只读镜像不允许定位到文件尾之后。

### 5.5 目录遍历

ROFS 没有目录节点，目录由记录路径前缀表达：

```
config/device.json
config/net.json
config/backup/data.json
www/index.html
```

`rofs_readdir()` 对当前目录前缀 `P`：

1. 顺序扫描记录；
2. 根目录取完整路径作为候选；子目录要求 `name` 以 `P + "/"` 开头；
3. 截掉前缀后若仍含 `/`，只返回第一段并标记为 DIR；
4. 否则按记录类型返回 FILE 或显式 DIR；
5. 使用句柄中的 `last` 去掉**连续重复**的子目录名。

由于 Builder 按路径排序，同一子目录的记录必然相邻，因此“只比较上一次
返回的名称”即可完成去重，不需要额外索引或缓存。

返回约定：`0=有项`、`1=遍历结束`、负值=错误。

### 5.6 路径规则

| 规则 | 说明 |
| --- | --- |
| 相对路径 | 不以 `/` 开头/结尾 |
| 分隔符 | 固定 `/` |
| 空段 | 禁止（如 `a//b`） |
| `.` / `..` | 禁止 |
| 长度 | ≤ 255 字节 |
| 大小写 | 敏感 |
| opendir | 额外接受 `""`、`"/"`、`"/config"`、`config/` 写法并归一化 |

---

## 6. C API

### 6.1 生命周期

```c
int rofs_init(rofs_t *fs);
int rofs_mount(rofs_t *fs, uint32_t base_offset,
               rofs_read_fn read, void *ctx);
int rofs_unmount(rofs_t *fs);
int rofs_get_info(rofs_t *fs, rofs_info_t *info);
int rofs_check(rofs_t *fs);
```

### 6.2 文件

```c
int      rofs_open(rofs_t *fs, const char *path, rofs_file_t *file);
int      rofs_close(rofs_file_t *file);
int32_t  rofs_read(rofs_file_t *file, void *buffer, uint32_t size);
int      rofs_seek(rofs_file_t *file, int32_t offset, int whence);
uint32_t rofs_tell(rofs_file_t *file);
uint32_t rofs_size(rofs_file_t *file);
int      rofs_eof(rofs_file_t *file);
int      rofs_exists(rofs_t *fs, const char *path);
int      rofs_stat(rofs_t *fs, const char *path, rofs_stat_t *stat);
```

### 6.3 目录

```c
int rofs_opendir(rofs_t *fs, const char *path, rofs_dir_t *dir);
int rofs_readdir(rofs_dir_t *dir, rofs_dirent_t *entry);
int rofs_rewinddir(rofs_dir_t *dir);
int rofs_closedir(rofs_dir_t *dir);
```

### 6.4 返回值约定

- 生命周期/目录/命名空间接口：`0` 成功，负 `ROFS_ERR_*` 失败；
- `rofs_read`：正数为字节数，`0` 表示 EOF，负数为错误；
- `rofs_readdir`：`0` 有项、`1` 结束、负值错误；
- `rofs_exists` / `rofs_eof`：`1` 表示是，`0` 表示否。

### 6.5 错误码

| 错误码 | 含义 |
| --- | --- |
| `ROFS_ERR_PARAM` | 空指针等参数错误 |
| `ROFS_ERR_IO` | 底层读取失败 |
| `ROFS_ERR_MAGIC` | 魔数不匹配 |
| `ROFS_ERR_VERSION` | 版本不支持 |
| `ROFS_ERR_FORMAT` | 头部/记录区/END 结构非法 |
| `ROFS_ERR_CRC` | 记录或镜像 CRC 失败 |
| `ROFS_ERR_RANGE` | 偏移越界 |
| `ROFS_ERR_NOT_FOUND` | 路径不存在 |
| `ROFS_ERR_IS_DIR` | 目标是目录 |
| `ROFS_ERR_INVALID` | 路径非法/未挂载/句柄无效 |

---

## 7. 资源占用

### 7.1 代码体积（arm-none-eabi-gcc，-Os，对象级）

| 对象 | text |
| --- | --- |
| rofs.c | 704B |
| rofs_file.c | 1,130B |
| rofs_dir.c | 736B |
| **合计** | **约 2.6KB** |

链接时若未被引用会被 `--gc-sections` 完全裁剪；实际接入固件后
预计占用 2~3KB Flash。

### 7.2 RAM

| 对象 | 估算（32 位 MCU） |
| --- | --- |
| `rofs_t` | 约 40B |
| `rofs_file_t` | 约 20B |
| `rofs_dir_t` | 约 528B（含 2×256B 名称缓冲） |
| 单次调用栈缓冲 | 约 272B（文件名 256B + 记录头 16B） |

不使用 malloc；文件/目录句柄由调用者静态分配。

---

## 8. 使用示例

### 8.1 生成镜像

```bash
python tools/make_rofs.py build ./resource ./resource.bin
python tools/make_rofs.py verify ./resource.bin
```

### 8.2 烧写到 Flash

把 `resource.bin` 烧写到预留分区（例如 `0x08040000` 或 SPI Flash 的
某个偏移），分区大小不小于镜像大小。

### 8.3 MCU 端挂载

```c
#include "rofs.h"

#define ROFS_FLASH_ADDR  0x08040000u

static rofs_t s_rofs;

static int flash_read(void *ctx, uint32_t offset, void *buf, uint32_t size)
{
    memcpy(buf, (const void *)offset, size);   /* 内部 Flash 直接映射 */
    return 0;
}

void app_resource_init(void)
{
    rofs_file_t f;
    char buf[128];
    int n;

    if (rofs_mount(&s_rofs, ROFS_FLASH_ADDR, flash_read, NULL) != ROFS_OK) {
        return;
    }
    if (rofs_open(&s_rofs, "config/device.json", &f) == ROFS_OK) {
        n = rofs_read(&f, buf, sizeof(buf));
        rofs_close(&f);
    }
}
```

对 SPI Flash，只需把 `flash_read` 换成对应的 `w25qxx_read` 封装。

---

## 9. 测试

`components/rofs/rofs_test.c` 在 RAM 中构造镜像，覆盖 6 组：

| 用例 | 覆盖点 |
| --- | --- |
| rofs/mount-info | 挂载、镜像信息、完整校验 |
| rofs/file-read | open/read/seek/tell/size/eof |
| rofs/stat-exists | exists/stat、目录打开、非法路径 |
| rofs/dir-root | 根目录遍历、子目录项、rewinddir |
| rofs/dir-sub | 多级目录遍历、缺失目录 |
| rofs/corrupt | Body CRC 损坏、魔数错误、END 错误、恢复挂载 |

`ros-test` 结果：5085 项检查、0 失败。

Python 工具额外验证：

- `build/info/list/extract/verify` 全流程；
- C 端挂载 Python 生成的镜像：`mount=0 check=0`，成功读取并遍历目录，
  两端格式与 CRC 完全一致。

---

## 10. 已知限制

1. 只读：运行期不能新增/修改/删除文件；
2. 文件查找为顺序扫描，文件数量很多时 O(n) 延迟会上升；
3. 目录由路径前缀推导，**空目录不会保留**（除非手工写入 DIR 记录）；
4. `readdir` 的子目录去重依赖镜像按路径排序（Builder 已保证）；
5. 单条记录大小受 32 位 `size` 限制（4GB），实际受分区容量限制；
6. 不支持压缩、加密、分片与增量更新；
7. 镜像更新需要整包重新烧写。

---

## 11. 与 MFS / littlefs 的关系

| 项目 | ROFS（本组件） | MFS（components/mfs） | littlefs（vendor） |
| --- | --- | --- | --- |
| 读写 | 只读 | 可读写 | 可读写 |
| 数据来源 | PC 工具生成镜像 | 运行期写入 | 运行期写入 |
| 元数据 | 顺序记录 | 日志记录 + Inode | Metadata pair + CTZ |
| GC/磨损 | 不需要 | 有 | 有 |
| RAM | 极小、无 malloc | 约 0.6KB | 较大、默认使用堆 |
| 适用 | 静态资源包 | 设备运行期配置/日志 | 通用可写文件系统 |

三者可以共存：静态资源用 ROFS，运行期数据用 MFS 或 littlefs。
