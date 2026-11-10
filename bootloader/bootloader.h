#ifndef BOOTLOADER_H
#define BOOTLOADER_H

/*
 * bootloader.h - Bootloader 公共逻辑（与平台无关）
 *
 * 本模块只依赖「Flash 操作」与「跳转/串口」两个移植接口，负责：
 *   分区表   partition_*  地址/大小/可写性，读写擦除经平台注册的 Flash 操作；
 *   镜像格式 image_*      固定头 + 负载 CRC32 全量校验；
 *   升级会话 upgrade_*    OTA 分区擦除、接收、校验、就绪（供 FOTA 调用）；
 *   启动流程 boot_*       升级/回滚标志、镜像选择、OTA->APP 安装、跳转；
 *   串口升级 boot_uart_*  请求帧握手 + 分块接收 + 逐块 CRC + 整镜像校验。
 *
 * 平台侧（platforms/<plat>/ 下的 *_bootloader.c）只需实现文件末尾的移植接口：
 *   boot_port_init / boot_register_flash / boot_port_ram_* / boot_port_jump。
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================== 移植接口 ============================== */

/* 平台 Flash 操作：地址为绝对地址，写前由调用方保证已擦除 */
typedef struct {
    uint32_t start;         /* Flash 起始地址 */
    uint32_t size;          /* 可操作区域大小 */
    uint32_t erase_size;    /* 擦除粒度（字节） */

    int (*read)(uint32_t address, void *buffer, uint32_t length);
    int (*write)(uint32_t address, const void *buffer, uint32_t length);
    int (*erase)(uint32_t address, uint32_t length);
} boot_flash_ops_t;

/*
 * 平台实现：初始化并注册 Flash 操作与默认分区。
 * 由公共逻辑首次访问分区时惰性调用一次。
 */
void boot_port_init(void);

/* 平台实现：APP 可用 SRAM 范围（跳转前校验栈顶） */
uint32_t boot_port_ram_start(void);
uint32_t boot_port_ram_end(void);

/*
 * 平台实现：切换到 entry 指向的向量表并跳转。
 * 返回 0 表示已跳转（通常不返回）；平台不支持跳转返回 -1。
 */
int boot_port_jump(uint32_t entry);

/* 平台在 boot_port_init() 中调用，登记 Flash 操作 */
int boot_register_flash(const boot_flash_ops_t *ops);

/* ============================== 分区 ============================== */

typedef enum {
    PART_BOOT,
    PART_APP,
    PART_APP_BACKUP,
    PART_OTA,
    PART_CONFIG,
    PART_LOG
} partition_type_t;

typedef struct {
    partition_type_t type;
    uint32_t address;       /* Flash 绝对地址 */
    uint32_t size;          /* 字节数 */
    uint32_t erase_size;    /* 擦除粒度（字节） */
    bool writable;          /* false 表示 Bootloader/APP 代码区保护 */
} partition_t;

/*
 * 注册（或按 type 覆盖）分区。地址必须落在已注册 Flash 区域之内。
 * 返回 0 成功；-1 参数非法或 Flash 未就绪。
 */
int partition_register(const partition_t *partition);

/* 按类型查找分区；不存在返回 NULL。 */
const partition_t *partition_get(partition_type_t type);

/* 分区内相对偏移读写（写前必须已擦除）；返回 0 成功 / -1 失败。 */
int partition_read(partition_type_t type, uint32_t offset,
                   void *buffer, uint32_t len);
int partition_write(partition_type_t type, uint32_t offset,
                    const void *buffer, uint32_t len);

/* 擦除 [offset, offset+len)，自动按 erase_size 向两侧对齐。 */
int partition_erase(partition_type_t type, uint32_t offset, uint32_t len);

/* 擦除整个分区。 */
int partition_erase_all(partition_type_t type);

/* ============================== 镜像 ============================== */

#define IMAGE_MAGIC 0x494F5449u   /* "IOTI" */

/*
 * 镜像头（28 字节）：紧跟其后的 image_size 字节为负载，
 * crc32 覆盖负载全部字节（CRC-32/IEEE，多项式 0xEDB88320）。
 */
typedef struct {
    uint32_t magic;         /* IMAGE_MAGIC */
    uint16_t header_size;   /* 必须 >= sizeof(image_header_t) */
    uint16_t header_version;/* 头部格式版本，当前为 1 */
    uint32_t product_id;
    uint32_t hardware_id;
    uint32_t version;       /* 固件版本 */
    uint32_t image_size;    /* 负载字节数（不含头） */
    uint32_t crc32;         /* 负载 CRC32 */
    uint32_t flags;
} image_header_t;

typedef enum {
    IMAGE_INVALID,
    IMAGE_VALID
} image_state_t;

int image_parse(const void *data, uint32_t len, image_header_t *header);
int image_verify(const image_header_t *header);

/* 读取并校验分区内镜像：头字段 + 负载 CRC32 全量回读。返回 0 / -1。 */
int image_verify_stored(partition_type_t type);

uint32_t image_crc32(const void *data, uint32_t len);
int image_get_state(image_state_t *state);

/* ============================== 升级 ============================== */

typedef enum {
    UPGRADE_IDLE,
    UPGRADE_PREPARE,
    UPGRADE_RECEIVING,
    UPGRADE_VERIFYING,
    UPGRADE_READY,
    UPGRADE_INSTALLING,
    UPGRADE_SUCCESS,
    UPGRADE_FAILED
} upgrade_state_t;

typedef struct {
    uint32_t version;
    uint32_t size;          /* 镜像总字节数（含头） */
    uint32_t offset;

    uint8_t sha256[32];     /* 预留：摘要/签名比对 */
    uint32_t hardware_id;
    uint32_t product_id;
} upgrade_info_t;

typedef struct {
    upgrade_state_t state;

    uint32_t total_size;
    uint32_t received_size;
    uint32_t current_offset;

    int32_t error;
} upgrade_status_t;

typedef void (*upgrade_progress_fn)(const upgrade_status_t *status, void *user);

int upgrade_init(void);
int upgrade_begin(const upgrade_info_t *info);
int upgrade_write(uint32_t offset, const void *data, uint32_t len);
int upgrade_resume(void);
int upgrade_verify(void);
int upgrade_finish(void);
int upgrade_abort(void);
int upgrade_get_status(upgrade_status_t *status);
int upgrade_set_callback(upgrade_progress_fn callback, void *user);

/* ============================== 启动 ============================== */

typedef enum {
    BOOT_NORMAL,        /* 直接启动 APP */
    BOOT_UPGRADE,       /* OTA 镜像已校验，安装后启动 */
    BOOT_RECOVERY,      /* OTA 镜像非法，等待重新下发 */
    BOOT_ROLLBACK       /* 回滚到备份镜像 */
} boot_mode_t;

/* 自定义跳转钩子；为空时走平台 boot_port_jump()（便于测试注入） */
typedef void (*boot_jump_fn)(void);
int boot_set_jump(boot_jump_fn jump);

int boot_init(void);
boot_mode_t boot_get_mode(void);
int boot_select_image(void);
int boot_verify_image(void);
int boot_install_image(void);
int boot_start_application(void);

int boot_request_upgrade(void);
int boot_request_rollback(void);
int boot_confirm_application(void);

/* ============================== 串口升级 ============================== */

/*
 * 协议（小端）：
 *   请求帧 20 字节（上位机 -> Bootloader）
 *     [0..3] 'I','O','T','U'   [4..5] 协议版本=1   [6..7] 保留
 *     [8..11] 固件版本   [12..15] 镜像总字节数（含 28 字节头）
 *     [16..19] 前 16 字节的 CRC32
 *   应答帧 8 字节（Bootloader -> 上位机）
 *     [0..3] 'I','O','T','A'   [4] 0=成功/1=失败   [5] 错误码
 *     [6..7] 对应块序号（握手与结束帧为 0xFFFF）
 *   数据帧 = 8 字节块头 + 负载：
 *     [0..1] 块序号（从 0 递增，0xFFFF=结束） [2..3] 负载长度(1..256)
 *     [4..7] 负载 CRC32
 */
#define BOOT_UART_REQ_SIZE    20U
#define BOOT_UART_REPLY_SIZE  8U
#define BOOT_UART_BLOCK_MAX   256U
#define BOOT_UART_PROTO       1U
#define BOOT_UART_SEQ_END     0xFFFFU

typedef enum {
    BOOT_UART_CODE_OK = 0,
    BOOT_UART_CODE_CRC,
    BOOT_UART_CODE_SIZE,
    BOOT_UART_CODE_SEQ,
    BOOT_UART_CODE_FLASH,
    BOOT_UART_CODE_VERIFY,
    BOOT_UART_CODE_PROTO
} boot_uart_code_t;

/* 串口端口：读取最多 len 字节，超时返回 0，负值表示端口错误 */
typedef int (*boot_uart_read_fn)(uint8_t *buf, uint32_t len,
                                 uint32_t timeout_ms, void *user);
typedef int (*boot_uart_write_fn)(const uint8_t *buf, uint32_t len,
                                  uint32_t timeout_ms, void *user);

void boot_set_uart(boot_uart_read_fn rd, boot_uart_write_fn wr, void *user);

/* 设置串口 IO 超时（0 表示使用默认 5s），平台绑定 hal_uart 时调用 */
void boot_set_uart_timeout(uint32_t timeout_ms);

/*
 * 启动后等待串口升级命令并完成接收/校验/置位升级标志。
 * 返回 0 = 升级完成（应复位进入安装）；1 = 未收到请求；
 *      -1 = 协议/写入/校验失败（已回 NAK）；-2 = 未配置串口端口。
 */
int boot_uart_wait_update(uint32_t wait_ms);

#ifdef __cplusplus
}
#endif

#endif /* BOOTLOADER_H */
