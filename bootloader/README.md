# Bootloader

片内 Flash 的启动、升级与镜像校验模块。分两层：

```
bootloader/                平台无关：分区表、镜像格式、升级状态机、串口升级协议
platforms/<plat>/          平台差异：Flash 读写擦除、SRAM 范围、向量表跳转、串口绑定
```

当前提供 `platforms/stm32/stm32_bootloader.c`（真实 Flash + 跳转 + hal_uart）与
`platforms/pc/pc_bootloader.c`（RAM 模拟 Flash，供 host 测试）。新增平台只需实现
[移植契约](#4-移植契约)。

---

## 1. 功能概览

| 能力 | 接口 | 说明 |
| --- | --- | --- |
| 分区管理 | `partition_register/get/read/write/erase/erase_all` | 地址、大小、可写性；擦除自动按页对齐 |
| 镜像校验 | `image_parse/verify/verify_stored/crc32` | 28 字节头 + 负载 CRC-32/IEEE 全量回读 |
| 升级会话 | `upgrade_init/begin/write/resume/verify/finish/abort` | 擦除 OTA、接收、长度一致性检查、CRC 校验 |
| 启动控制 | `boot_init/select_image/verify_image/install_image/start_application` | 升级/回滚标志持久化，OTA→APP 安装，跳转 APP |
| 串口升级 | `boot_set_uart`、`boot_uart_wait_update` | 握手 + 分块接收 + 逐块 CRC + 重传 |
| 状态查询 | `upgrade_get_status`、`upgrade_set_callback` | 进度回调（状态/总量/已收/错误码） |

---

## 2. 默认 Flash 布局（64KB）

| 分区 | 偏移 | 大小 | 可写 | 用途 |
| --- | --- | --- | --- | --- |
| `PART_BOOT` | 0x00000 | 16 KB | 否 | Bootloader 自身 |
| `PART_APP` | 0x04000 | 28 KB | 是 | 应用镜像（由 Bootloader 写入） |
| `PART_OTA` | 0x0B000 | 16 KB | 是 | 升级包暂存区 |
| `PART_CONFIG` | 0x0F000 | 4 KB | 是 | 升级/回滚标志（magic + mode + CRC32） |

偏移宏在 `platforms/stm32/stm32_bootloader.h`（`STM32_BOOT_OFF_*` / `STM32_BOOT_SIZE_*`）
可直接在工程内 `-D` 覆盖；运行时也可用 `partition_register()` 覆盖同类型分区。
`partition_register()` 会校验地址必须落在平台登记的 Flash 区域内。

---

## 3. 镜像格式

```c
typedef struct {                 /* 28 字节，紧跟在后的是 image_size 字节负载 */
    uint32_t magic;              /* 'IOTI' = 0x494F5449 */
    uint16_t header_size;        /* >= 28，头部+负载的总长 = header_size + image_size */
    uint16_t header_version;     /* 当前 1 */
    uint32_t product_id;
    uint32_t hardware_id;
    uint32_t version;            /* 固件版本 */
    uint32_t image_size;         /* 负载字节数（不含头） */
    uint32_t crc32;              /* 负载的 CRC-32/IEEE */
    uint32_t flags;
} image_header_t;
```

校验规则（`image_verify_stored()`）：

1. 头字段合法：magic、`header_size >= 28`、`header_version == 1`、`image_size > 0`
2. `header_size + image_size` 不超过分区容量
3. 按块回读负载计算 CRC32，与头中 `crc32` 一致

> 注意：安装时整个 OTA（头 + 负载）原样拷到 APP 起始地址，跳转入口为
> `APP 起始 + header_size`。若应用镜像是不带头的裸 bin，需要调整
> `boot_install_image()` 与 `boot_start_application()` 的偏移。

---

## 4. 移植契约

平台层只需实现 4 个函数，并可用 `boot_register_flash()` 登记 Flash 操作：

```c
void     boot_port_init(void);              /* 初始化 Flash、登记默认分区（首次访问时惰性调用） */
uint32_t boot_port_ram_start(void);         /* 跳转前校验栈顶的 SRAM 范围 */
uint32_t boot_port_ram_end(void);
int      boot_port_jump(uint32_t entry);    /* 切向量表并跳转；不支持跳转返回 -1 */
```

Flash 操作结构：

```c
typedef struct {
    uint32_t start;       /* Flash 起始地址 */
    uint32_t size;        /* 可操作区域大小 */
    uint32_t erase_size;  /* 擦除粒度 */
    int (*read)(uint32_t address, void *buffer, uint32_t length);
    int (*write)(uint32_t address, const void *buffer, uint32_t length);
    int (*erase)(uint32_t address, uint32_t length);
} boot_flash_ops_t;
```

STM32 参考实现使用 `hal_flash_*`（内部已处理解锁/上锁），跳转前校验
`SP ∈ [RAM_START, RAM_END]`、`PC` 位于 Flash 且为 Thumb 地址，然后
`msr msp` + 跳转复位向量。host 端 `boot_port_jump()` 直接返回 -1，
测试用 `boot_set_jump()` 注入假跳转。

---

## 5. 启动流程

```c
boot_init();                        /* 读 CONFIG 标志 + 初始化分区表 */
/* 可选：串口等待升级，返回 0 表示已收到完整镜像，应复位进入安装 */
if (boot_uart_wait_update(3000U) == 0) {
    boot_install_image();
    /* 复位 */
}

boot_select_image();                /* 决定 NORMAL / UPGRADE / RECOVERY / ROLLBACK */
if (boot_get_mode() == BOOT_UPGRADE) {
    boot_install_image();           /* 擦除 APP + 搬运 + 回读 CRC 校验 */
}
boot_start_application();           /* 校验向量表后跳转 APP */
```

模式判定：

| 当前模式 | 条件 | 行为 |
| --- | --- | --- |
| `BOOT_ROLLBACK` | 收到回滚请求 | 保持回滚意图（由上层决定安装哪个镜像） |
| `BOOT_UPGRADE` | 收到升级请求 | OTA 校验通过则保持；失败转 `BOOT_RECOVERY` |
| `BOOT_NORMAL` | 无请求 | APP 校验失败且 OTA 有效时自动转为 `BOOT_UPGRADE` |

APP 启动成功后调用 `boot_confirm_application()` 清除标志，避免反复回滚。

安装与跳转的返回值：`0` 成功；`-1` 分区缺失/镜像非法/容量不足/Flash 操作失败。

---

## 6. 升级流程（FOTA / 上位机）

### 6.1 调用 upgrade 会话

```c
upgrade_info_t info = {0};
info.version = 0x0102;
info.size    = 镜像总字节数;

upgrade_begin(&info);        /* 擦除整个 OTA 分区 */
upgrade_write(0, buf, n);    /* 按偏移写入，可多次 */
upgrade_verify();            /* 收齐 + 头合法 + 长度一致 + CRC 正确 */
upgrade_finish();            /* 标记 READY -> SUCCESS */
boot_request_upgrade();      /* 写 CONFIG 标志，下次复位进入安装 */
```

失败路径会进入 `UPGRADE_FAILED`，`upgrade_abort()` 可回到 `UPGRADE_IDLE`
（不清除已写入的数据）。

### 6.2 串口升级协议

字节序为小端。

**请求帧（上位机 → Bootloader，20 字节）**

| 偏移 | 长度 | 内容 |
| --- | --- | --- |
| 0 | 4 | `'I' 'O' 'T' 'U'` |
| 4 | 2 | 协议版本，固定 1 |
| 6 | 2 | 保留，填 0 |
| 8 | 4 | 固件版本 |
| 12 | 4 | 镜像总字节数（含 28 字节头） |
| 16 | 4 | 前 16 字节的 CRC32 |

**应答帧（Bootloader → 上位机，8 字节）**

| 偏移 | 长度 | 内容 |
| --- | --- | --- |
| 0 | 4 | `'I' 'O' 'T' 'A'` |
| 4 | 1 | `0` 成功 / `1` 失败 |
| 5 | 1 | 错误码（见下表） |
| 6 | 2 | 对应块序号；握手与结束帧为 `0xFFFF` |

**数据帧（上位机 → Bootloader）**

| 偏移 | 长度 | 内容 |
| --- | --- | --- |
| 0 | 2 | 块序号，从 0 递增；`0xFFFF` 表示结束 |
| 2 | 2 | 负载长度（1..256；结束帧为 0） |
| 4 | 4 | 负载 CRC32 |
| 8 | N | 负载 |

**错误码**

| 值 | 含义 |
| --- | --- |
| 0 | OK |
| 1 | 块/握手 CRC 错误 |
| 2 | 长度错误（总长不符、负载过长） |
| 3 | 块序号跳跃 |
| 4 | Flash 写入失败 |
| 5 | 整镜像校验失败 |
| 6 | 协议版本/帧格式错误 |

交互时序：

```
上位机                          Bootloader
  |-- 请求帧(20B) ------------->|  校验 magic/协议/CRC
  |<----------- ACK(seq=0xFFFF) |  擦除 OTA，进入接收
  |-- 数据帧 #0 --------------->|  CRC 错 -> NAK(CRC)，等待重发同块
  |<-------------- ACK(seq=0)   |  写入 OTA
  |-- 数据帧 #1 --------------->|
  |<-------------- ACK(seq=1)   |
  |-- 结束帧(seq=0xFFFF,len=0)->|  长度一致性 + 整镜像 CRC
  |<----------- ACK(seq=0xFFFF) |  置升级标志，返回 0
```

行为约定：

- 同一块重发时序号小于期望值 → 直接回 ACK（幂等），不重复写入
- 序号跳跃 → NAK(SEQ) 并终止本次升级
- 读取超时（默认 5s，可用 `boot_set_uart_timeout()` 调整）→ 终止并 `upgrade_abort()`

### 6.3 串口端口绑定

```c
/* STM32：使用已初始化的 hal_uart_t（weak 引用，未启用 HAL 也能链接） */
boot_set_uart_handle(&uart1, 100U);

/* 任意平台：注入自定义收发 */
boot_set_uart(my_read, my_write, my_ctx);
```

`boot_uart_wait_update(wait_ms)` 返回值：`0` 升级完成（应复位）、`1` 未收到请求
（正常启动）、`-1` 协议/写入/校验失败（已回 NAK）、`-2` 未配置串口端口。

---

## 7. 配置宏

| 宏 | 默认 | 说明 |
| --- | --- | --- |
| `STM32_BOOT_OFF_*` / `STM32_BOOT_SIZE_*` | 见第 2 节 | 默认分区布局 |
| `STM32_BOOT_RAM_START` / `_END` | 0x20000000 / 0x20005000 | 跳转前栈顶校验范围（F103C8 20KB） |
| `BOOT_UART_IO_TIMEOUT_MS` | 5000 | 串口单次读超时 |
| `BOOT_UART_BLOCK_MAX` | 256 | 单块负载上限 |

---

## 8. 测试

`bootloader/bootloader_test.c` 在 host 端通过 `platforms/pc` 的 RAM 模拟 Flash 运行：

```bash
xmake build ros-test
xmake run ros-test
```

覆盖：分区注册/越界拒绝/读写擦除、CRC-32 标准向量、篡改检测、升级全流程、
OTA→APP 安装与回读校验、启动标志、串口升级（无请求、握手 CRC 错、块重传、
序号跳跃）。
