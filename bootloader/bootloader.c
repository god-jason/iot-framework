/*
 * bootloader.c - Bootloader 公共逻辑（平台无关）
 *
 * 平台差异全部收敛到四个移植接口：
 *   boot_port_init()         初始化并 boot_register_flash()、注册默认分区
 *   boot_port_ram_start/end  跳转前校验栈顶的 SRAM 范围
 *   boot_port_jump()         切换向量表并跳转 APP
 * 串口收发通过 boot_set_uart() 注入，STM32 的 hal_uart 绑定在平台目录。
 */

#include "bootloader.h"
#include "ros.h"
#include "common.h"

#include <string.h>

#define BOOT_TAG "boot"

#define BOOT_PART_MAX    8U
#define BOOT_CHUNK_SIZE  256U    /* 安装/校验时的搬运块 */

/* ------------------------------ 内部状态 ------------------------------ */

static boot_flash_ops_t s_flash;
static uint32_t         s_flash_gran = 1U;
static int              s_flash_state;   /* 0=未初始化 1=初始化中 2=就绪 */

static partition_t s_parts[BOOT_PART_MAX];
static uint32_t    s_part_count;

static image_state_t s_image_state = IMAGE_INVALID;

static upgrade_state_t s_up_state = UPGRADE_IDLE;
static uint32_t        s_up_total;
static uint32_t        s_up_received;
static int32_t         s_up_error;
static upgrade_progress_fn s_up_cb;
static void               *s_up_user;

static boot_jump_fn s_jump;
static boot_mode_t  s_boot_mode = BOOT_NORMAL;

/* 启动标志持久化记录（CRC 覆盖前 8 字节） */
#define BOOT_FLAG_MAGIC 0x424F4F54U   /* "BOOT" */

typedef struct {
    uint32_t magic;
    uint32_t mode;
    uint32_t crc;
} boot_flag_t;

/* ------------------------------ CRC32 ------------------------------ */

static uint32_t boot_crc32_update(uint32_t crc, const void *data, uint32_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t i;

    for (i = 0U; i < len; i++) {
        int bit;

        crc ^= (uint32_t)p[i];
        for (bit = 0; bit < 8; bit++) {
            crc = ((crc & 1U) != 0U) ? ((crc >> 1) ^ 0xEDB88320U) : (crc >> 1);
        }
    }
    return crc;
}

uint32_t image_crc32(const void *data, uint32_t len)
{
    if (data == NULL && len != 0U) {
        return 0U;
    }
    return boot_crc32_update(0xFFFFFFFFU, data, len) ^ 0xFFFFFFFFU;
}

/* ------------------------------ Flash 注册 ------------------------------ */

int boot_register_flash(const boot_flash_ops_t *ops)
{
    if (ops == NULL || ops->read == NULL || ops->write == NULL ||
        ops->erase == NULL || ops->size == 0U) {
        return -1;
    }

    s_flash = *ops;
    s_flash_gran = (ops->erase_size != 0U) ? ops->erase_size : 1U;
    s_flash_state = 2;
    return 0;
}

/* 首次访问时惰性调用平台初始化；端口在初始化中注册分区会递归进入 */
static int boot_flash_ensure(void)
{
    if (s_flash_state == 2) {
        return 0;
    }
    if (s_flash_state == 1) {
        return 0;               /* 端口正在注册 Flash/分区 */
    }

    s_flash_state = 1;
    boot_port_init();
    if (s_flash_state != 2) {
        LOG_E(BOOT_TAG, "flash not registered by platform");
        s_flash_state = 0;
        return -1;
    }
    return 0;
}

/* ------------------------------ 分区 ------------------------------ */

static partition_t *boot_part_find(partition_type_t type)
{
    uint32_t i;

    for (i = 0U; i < s_part_count; i++) {
        if (s_parts[i].type == type) {
            return &s_parts[i];
        }
    }
    return NULL;
}

int partition_register(const partition_t *partition)
{
    partition_t *slot;
    uint32_t offset;

    if (partition == NULL || partition->size == 0U) {
        return -1;
    }
    if (boot_flash_ensure() != 0) {
        return -1;
    }
    if (partition->address < s_flash.start) {
        return -1;
    }

    offset = partition->address - s_flash.start;
    if (offset >= s_flash.size || partition->size > s_flash.size - offset) {
        LOG_E(BOOT_TAG, "partition out of flash: type=%d addr=0x%X size=%u",
              (int)partition->type, (unsigned)partition->address,
              (unsigned)partition->size);
        return -1;
    }

    slot = boot_part_find(partition->type);
    if (slot == NULL) {
        if (s_part_count >= BOOT_PART_MAX) {
            return -1;
        }
        slot = &s_parts[s_part_count++];
    }
    *slot = *partition;
    return 0;
}

const partition_t *partition_get(partition_type_t type)
{
    if (boot_flash_ensure() != 0) {
        return NULL;
    }
    return boot_part_find(type);
}

int partition_read(partition_type_t type, uint32_t offset,
                   void *buffer, uint32_t len)
{
    const partition_t *p = partition_get(type);

    if (p == NULL || buffer == NULL || offset > p->size ||
        len > p->size - offset) {
        return -1;
    }
    if (len == 0U) {
        return 0;
    }
    return (s_flash.read(p->address + offset, buffer, len) == 0) ? 0 : -1;
}

int partition_write(partition_type_t type, uint32_t offset,
                    const void *buffer, uint32_t len)
{
    const partition_t *p = partition_get(type);

    if (p == NULL || !p->writable || (buffer == NULL && len != 0U) ||
        offset > p->size || len > p->size - offset) {
        return -1;
    }
    if (len == 0U) {
        return 0;
    }
    return (s_flash.write(p->address + offset, buffer, len) == 0) ? 0 : -1;
}

int partition_erase(partition_type_t type, uint32_t offset, uint32_t len)
{
    const partition_t *p = partition_get(type);
    uint32_t gran;
    uint32_t start;
    uint32_t end;

    if (p == NULL || offset > p->size || len > p->size - offset) {
        return -1;
    }
    if (len == 0U) {
        return 0;
    }

    gran = (p->erase_size != 0U) ? p->erase_size : s_flash_gran;
    start = offset - (offset % gran);          /* 向下对齐到擦除页 */
    end = offset + len;
    end = ((end + gran - 1U) / gran) * gran;   /* 向上对齐 */
    if (end > p->size) {
        end = p->size;
    }

    return (s_flash.erase(p->address + start, end - start) == 0) ? 0 : -1;
}

int partition_erase_all(partition_type_t type)
{
    const partition_t *p = partition_get(type);

    if (p == NULL) {
        return -1;
    }
    return partition_erase(type, 0U, p->size);
}

/* ------------------------------ 镜像 ------------------------------ */

int image_parse(const void *data, uint32_t len, image_header_t *header)
{
    if (data == NULL || header == NULL || len < sizeof(image_header_t)) {
        return -1;
    }
    memcpy(header, (const void *)data, sizeof(*header));
    return 0;
}

int image_verify(const image_header_t *header)
{
    if (header == NULL) {
        s_image_state = IMAGE_INVALID;
        return -1;
    }
    if (header->magic != IMAGE_MAGIC ||
        header->header_size < sizeof(image_header_t) ||
        header->header_version != 1U ||
        header->image_size == 0U) {
        s_image_state = IMAGE_INVALID;
        return -1;
    }
    s_image_state = IMAGE_VALID;
    return 0;
}

int image_get_state(image_state_t *state)
{
    if (state == NULL) {
        return -1;
    }
    *state = s_image_state;
    return 0;
}

/* 读取分区头部并做字段校验 */
static int boot_read_header(partition_type_t type, image_header_t *header)
{
    const partition_t *p = partition_get(type);

    if (p == NULL || header == NULL) {
        return -1;
    }
    if (partition_read(type, 0U, header, (uint32_t)sizeof(*header)) != 0) {
        return -1;
    }
    return image_verify(header);
}

int image_verify_stored(partition_type_t type)
{
    image_header_t header;
    const partition_t *p = partition_get(type);
    uint8_t block[BOOT_CHUNK_SIZE];
    uint32_t offset = 0U;
    uint32_t crc = 0xFFFFFFFFU;

    if (p == NULL || boot_read_header(type, &header) != 0) {
        s_image_state = IMAGE_INVALID;
        return -1;
    }
    if ((uint32_t)header.header_size + header.image_size > p->size) {
        s_image_state = IMAGE_INVALID;
        return -1;
    }

    /* 全量回读负载并计算 CRC，避免只信头部声明 */
    while (offset < header.image_size) {
        uint32_t n = header.image_size - offset;

        if (n > sizeof(block)) {
            n = (uint32_t)sizeof(block);
        }
        if (partition_read(type, (uint32_t)header.header_size + offset,
                           block, n) != 0) {
            s_image_state = IMAGE_INVALID;
            return -1;
        }
        crc = boot_crc32_update(crc, block, n);
        offset += n;
    }

    if ((crc ^ 0xFFFFFFFFU) != header.crc32) {
        LOG_E(BOOT_TAG, "image crc mismatch: type=%d calc=0x%X expect=0x%X",
              (int)type, (unsigned)(crc ^ 0xFFFFFFFFU), (unsigned)header.crc32);
        s_image_state = IMAGE_INVALID;
        return -1;
    }

    s_image_state = IMAGE_VALID;
    return 0;
}

/* ------------------------------ 升级会话 ------------------------------ */

static void upgrade_progress(void)
{
    upgrade_status_t status;

    if (s_up_cb == NULL) {
        return;
    }
    status.state = s_up_state;
    status.total_size = s_up_total;
    status.received_size = s_up_received;
    status.current_offset = s_up_received;
    status.error = s_up_error;
    s_up_cb(&status, s_up_user);
}

static int upgrade_fail(void)
{
    s_up_state = UPGRADE_FAILED;
    s_up_error = -1;
    upgrade_progress();
    return -1;
}

int upgrade_init(void)
{
    s_up_state = UPGRADE_IDLE;
    s_up_total = 0U;
    s_up_received = 0U;
    s_up_error = 0;
    s_up_cb = NULL;
    s_up_user = NULL;
    return 0;
}

int upgrade_begin(const upgrade_info_t *info)
{
    const partition_t *ota;

    if (info == NULL || info->size == 0U) {
        return -1;
    }
    ota = partition_get(PART_OTA);
    if (ota == NULL || !ota->writable || info->size > ota->size) {
        LOG_E(BOOT_TAG, "upgrade begin: ota partition invalid");
        return -1;
    }

    s_up_state = UPGRADE_PREPARE;
    upgrade_progress();

    /* 整个 OTA 分区擦除后再接收，保证旧镜像不会残留 */
    if (partition_erase_all(PART_OTA) != 0) {
        return upgrade_fail();
    }

    s_up_total = info->size;
    s_up_received = 0U;
    s_up_error = 0;
    s_up_state = UPGRADE_RECEIVING;
    upgrade_progress();
    return 0;
}

int upgrade_write(uint32_t offset, const void *data, uint32_t len)
{
    if (data == NULL || len == 0U || s_up_state != UPGRADE_RECEIVING) {
        return -1;
    }
    if (offset > s_up_total || len > s_up_total - offset) {
        return -1;
    }
    if (partition_write(PART_OTA, offset, data, len) != 0) {
        LOG_E(BOOT_TAG, "upgrade write failed: offset=%u len=%u",
              (unsigned)offset, (unsigned)len);
        return upgrade_fail();
    }

    if (offset + len > s_up_received) {
        s_up_received = offset + len;
    }
    upgrade_progress();
    return 0;
}

int upgrade_resume(void)
{
    if (s_up_state == UPGRADE_RECEIVING) {
        return 0;
    }
    s_up_state = UPGRADE_RECEIVING;
    upgrade_progress();
    return 0;
}

int upgrade_verify(void)
{
    image_header_t header;

    if (s_up_state != UPGRADE_RECEIVING && s_up_state != UPGRADE_READY) {
        return -1;
    }
    s_up_state = UPGRADE_VERIFYING;
    upgrade_progress();

    if (s_up_received < s_up_total) {
        return upgrade_fail();
    }
    if (boot_read_header(PART_OTA, &header) != 0) {
        return upgrade_fail();
    }
    /* 声明长度必须与本次会话接收长度一致，防止截断/拼接镜像 */
    if ((uint32_t)header.header_size + header.image_size != s_up_total) {
        LOG_E(BOOT_TAG, "upgrade verify: size mismatch %u != %u",
              (unsigned)((uint32_t)header.header_size + header.image_size),
              (unsigned)s_up_total);
        return upgrade_fail();
    }
    if (image_verify_stored(PART_OTA) != 0) {
        return upgrade_fail();
    }

    s_up_error = 0;
    s_up_state = UPGRADE_READY;
    upgrade_progress();
    return 0;
}

int upgrade_finish(void)
{
    if (s_up_state != UPGRADE_READY) {
        return -1;
    }
    s_up_state = UPGRADE_SUCCESS;
    upgrade_progress();
    return 0;
}

int upgrade_abort(void)
{
    s_up_state = UPGRADE_IDLE;
    upgrade_progress();
    return 0;
}

int upgrade_get_status(upgrade_status_t *status)
{
    if (status == NULL) {
        return -1;
    }
    status->state = s_up_state;
    status->total_size = s_up_total;
    status->received_size = s_up_received;
    status->current_offset = s_up_received;
    status->error = s_up_error;
    return 0;
}

int upgrade_set_callback(upgrade_progress_fn callback, void *user)
{
    s_up_cb = callback;
    s_up_user = user;
    return 0;
}

/* ------------------------------ 启动 ------------------------------ */

/* 启动标志写入 CONFIG 分区，掉电后仍可判断下一次启动模式 */
static void boot_flags_save(boot_mode_t mode)
{
    boot_flag_t flag;

    if (partition_get(PART_CONFIG) == NULL) {
        return;
    }

    flag.magic = BOOT_FLAG_MAGIC;
    flag.mode = (uint32_t)mode;
    flag.crc = image_crc32(&flag, 8U);

    if (partition_erase_all(PART_CONFIG) != 0 ||
        partition_write(PART_CONFIG, 0U, &flag, (uint32_t)sizeof(flag)) != 0) {
        LOG_W(BOOT_TAG, "save boot flag failed, mode=%d", (int)mode);
    }
}

static boot_mode_t boot_flags_load(void)
{
    boot_flag_t flag;

    if (partition_get(PART_CONFIG) == NULL ||
        partition_read(PART_CONFIG, 0U, &flag, (uint32_t)sizeof(flag)) != 0) {
        return BOOT_NORMAL;
    }
    if (flag.magic != BOOT_FLAG_MAGIC ||
        flag.crc != image_crc32(&flag, 8U) ||
        flag.mode > (uint32_t)BOOT_ROLLBACK) {
        return BOOT_NORMAL;
    }
    return (boot_mode_t)flag.mode;
}

int boot_set_jump(boot_jump_fn jump)
{
    s_jump = jump;
    return 0;
}

int boot_init(void)
{
    if (boot_flash_ensure() != 0) {
        return -1;
    }
    (void)upgrade_init();

    s_boot_mode = boot_flags_load();
    LOG_I(BOOT_TAG, "boot init, mode=%d flash=0x%X size=%u",
          (int)s_boot_mode, (unsigned)s_flash.start,
          (unsigned)s_flash.size);
    return 0;
}

boot_mode_t boot_get_mode(void)
{
    return s_boot_mode;
}

int boot_select_image(void)
{
    if (s_boot_mode == BOOT_ROLLBACK) {
        return 0;               /* 回滚请求优先，直接安装备份镜像 */
    }
    if (s_boot_mode == BOOT_UPGRADE) {
        if (image_verify_stored(PART_OTA) == 0) {
            return 0;           /* OTA 有效，保持升级模式 */
        }
        s_boot_mode = BOOT_RECOVERY;
        LOG_E(BOOT_TAG, "ota image invalid, enter recovery");
        return 0;
    }

    /* 无升级请求：校验现有 APP，损坏则尝试用 OTA 恢复 */
    if (image_verify_stored(PART_APP) != 0 &&
        image_verify_stored(PART_OTA) == 0) {
        s_boot_mode = BOOT_UPGRADE;
        LOG_W(BOOT_TAG, "app image invalid, fallback to ota");
    }
    return 0;
}

int boot_verify_image(void)
{
    return image_verify_stored(PART_OTA);
}

int boot_install_image(void)
{
    image_header_t header;
    const partition_t *app = partition_get(PART_APP);
    uint8_t block[BOOT_CHUNK_SIZE];
    uint32_t total;
    uint32_t offset;

    if (app == NULL || !app->writable) {
        return -1;
    }
    if (boot_read_header(PART_OTA, &header) != 0) {
        return -1;
    }

    total = (uint32_t)header.header_size + header.image_size;
    if (total > app->size) {
        LOG_E(BOOT_TAG, "install: image too large %u > %u",
              (unsigned)total, (unsigned)app->size);
        return -1;
    }
    if (partition_erase_all(PART_APP) != 0) {
        return -1;
    }

    /* OTA -> APP 分块搬运，先擦后写，避免出现半新半旧镜像 */
    for (offset = 0U; offset < total; ) {
        uint32_t n = total - offset;

        if (n > sizeof(block)) {
            n = (uint32_t)sizeof(block);
        }
        if (partition_read(PART_OTA, offset, block, n) != 0 ||
            partition_write(PART_APP, offset, block, n) != 0) {
            return -1;
        }
        offset += n;
    }

    /* 回读校验：拷贝过程中的任何位翻转都会在这里暴露 */
    if (image_verify_stored(PART_APP) != 0) {
        LOG_E(BOOT_TAG, "install: verify app failed");
        return -1;
    }

    LOG_I(BOOT_TAG, "install done, %u bytes", (unsigned)total);
    return 0;
}

int boot_start_application(void)
{
    image_header_t header;
    const partition_t *app;
    uint32_t vector[2];
    uint32_t entry;
    uint32_t sp;
    uint32_t pc;

    if (s_jump != NULL) {
        s_jump();               /* 测试/自定义平台注入的跳转 */
        return 0;
    }

    app = partition_get(PART_APP);
    if (app == NULL || boot_read_header(PART_APP, &header) != 0) {
        return -1;
    }
    entry = app->address + header.header_size;

    /* 通过 Flash 读接口取向量表，避免假定位映射（host 端为模拟 Flash） */
    if (s_flash.read(entry, vector, (uint32_t)sizeof(vector)) != 0) {
        return -1;
    }
    sp = vector[0];
    pc = vector[1];

    /* 栈顶必须落在 SRAM，复位地址必须落在 Flash 且为 Thumb */
    if (sp < boot_port_ram_start() || sp > boot_port_ram_end() ||
        (pc & 1U) == 0U ||
        pc < s_flash.start || pc >= s_flash.start + s_flash.size) {
        LOG_E(BOOT_TAG, "invalid app vector: sp=0x%X pc=0x%X",
              (unsigned)sp, (unsigned)pc);
        return -1;
    }

    LOG_I(BOOT_TAG, "jump to app: sp=0x%X pc=0x%X", (unsigned)sp, (unsigned)pc);
    return boot_port_jump(entry);
}

int boot_request_upgrade(void)
{
    s_boot_mode = BOOT_UPGRADE;
    boot_flags_save(s_boot_mode);
    return 0;
}

int boot_request_rollback(void)
{
    s_boot_mode = BOOT_ROLLBACK;
    boot_flags_save(s_boot_mode);
    return 0;
}

int boot_confirm_application(void)
{
    s_boot_mode = BOOT_NORMAL;
    boot_flags_save(s_boot_mode);
    return 0;
}

/* ------------------------------ 串口升级 ------------------------------ */

#ifndef BOOT_UART_IO_TIMEOUT_MS
#define BOOT_UART_IO_TIMEOUT_MS 5000U
#endif

static boot_uart_read_fn  s_uart_read;
static boot_uart_write_fn s_uart_write;
static void              *s_uart_user;
static uint32_t           s_uart_timeout_ms = BOOT_UART_IO_TIMEOUT_MS;

/* 小端读写：不依赖对齐，避免设备端跨编译器差异 */
static uint16_t boot_le16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t boot_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void boot_put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFU);
    p[1] = (uint8_t)((v >> 8) & 0xFFU);
}

void boot_set_uart(boot_uart_read_fn rd, boot_uart_write_fn wr, void *user)
{
    s_uart_read = rd;
    s_uart_write = wr;
    s_uart_user = user;
}

/* 设置 IO 超时（平台绑定 hal_uart 时使用） */
void boot_set_uart_timeout(uint32_t timeout_ms)
{
    s_uart_timeout_ms = (timeout_ms != 0U) ? timeout_ms
                                           : BOOT_UART_IO_TIMEOUT_MS;
}

/* 回复应答帧；写失败不影响主流程 */
static void boot_uart_reply(uint8_t failed, uint8_t code, uint16_t seq)
{
    uint8_t frame[BOOT_UART_REPLY_SIZE];

    frame[0] = 'I';
    frame[1] = 'O';
    frame[2] = 'T';
    frame[3] = 'A';
    frame[4] = failed;
    frame[5] = code;
    boot_put16(&frame[6], seq);

    if (s_uart_write != NULL) {
        (void)s_uart_write(frame, sizeof(frame), s_uart_timeout_ms, s_uart_user);
    }
}

/* 读满 len 字节；任一段超时即失败 */
static int boot_uart_read_exact(uint8_t *buf, uint32_t len)
{
    uint32_t got = 0U;

    while (got < len) {
        int n = s_uart_read(buf + got, len - got, s_uart_timeout_ms, s_uart_user);

        if (n <= 0) {
            return -1;
        }
        got += (uint32_t)n;
    }
    return 0;
}

/*
 * 接收数据帧并写入 OTA：
 *   - 块 CRC 错：回 NAK 后继续等待上位机重发同一块；
 *   - 重复块（seq < expected）：直接回 ACK，保证重发幂等；
 *   - 序号跳跃 / 超长 / 写入失败：回 NAK 并终止。
 */
static int boot_uart_receive_image(uint32_t total)
{
    static uint8_t payload[BOOT_UART_BLOCK_MAX];
    uint8_t header[8];
    uint32_t offset = 0U;
    uint16_t expected = 0U;

    for (;;) {
        uint16_t seq;
        uint16_t len;
        uint32_t crc;

        if (boot_uart_read_exact(header, sizeof(header)) != 0) {
            return -1;
        }
        seq = boot_le16(&header[0]);
        len = boot_le16(&header[2]);
        crc = boot_le32(&header[4]);

        if (seq == BOOT_UART_SEQ_END) {
            if (len != 0U || offset != total) {
                boot_uart_reply(1U, BOOT_UART_CODE_SIZE, seq);
                return -1;
            }
            return 0;
        }
        if (len == 0U || len > BOOT_UART_BLOCK_MAX) {
            boot_uart_reply(1U, BOOT_UART_CODE_SIZE, seq);
            return -1;
        }
        if (boot_uart_read_exact(payload, len) != 0) {
            return -1;
        }

        if (image_crc32(payload, len) != crc) {
            boot_uart_reply(1U, BOOT_UART_CODE_CRC, seq);
            continue;
        }
        if (seq < expected) {
            boot_uart_reply(0U, BOOT_UART_CODE_OK, seq);
            continue;
        }
        if (seq != expected) {
            boot_uart_reply(1U, BOOT_UART_CODE_SEQ, seq);
            return -1;
        }
        if (len > total - offset) {
            boot_uart_reply(1U, BOOT_UART_CODE_SIZE, seq);
            return -1;
        }
        if (upgrade_write(offset, payload, len) != 0) {
            boot_uart_reply(1U, BOOT_UART_CODE_FLASH, seq);
            return -1;
        }

        offset += len;
        expected++;
        boot_uart_reply(0U, BOOT_UART_CODE_OK, seq);
    }
}

int boot_uart_wait_update(uint32_t wait_ms)
{
    uint8_t frame[BOOT_UART_REQ_SIZE];
    upgrade_info_t info;
    uint32_t size;
    int n;

    if (s_uart_read == NULL || s_uart_write == NULL) {
        return -2;
    }
    if (boot_flash_ensure() != 0) {
        return -1;
    }

    /* 先等魔数：超时说明没有升级请求，正常启动 */
    n = s_uart_read(frame, 4U, wait_ms, s_uart_user);
    if (n <= 0) {
        return 1;
    }
    if ((uint32_t)n < 4U && boot_uart_read_exact(&frame[n], 4U - (uint32_t)n) != 0) {
        boot_uart_reply(1U, BOOT_UART_CODE_PROTO, BOOT_UART_SEQ_END);
        return -1;
    }
    if (frame[0] != 'I' || frame[1] != 'O' || frame[2] != 'T' || frame[3] != 'U') {
        return 1;               /* 不是升级帧，按无请求处理 */
    }
    if (boot_uart_read_exact(&frame[4], BOOT_UART_REQ_SIZE - 4U) != 0) {
        boot_uart_reply(1U, BOOT_UART_CODE_PROTO, BOOT_UART_SEQ_END);
        return -1;
    }
    if (boot_le16(&frame[4]) != BOOT_UART_PROTO) {
        boot_uart_reply(1U, BOOT_UART_CODE_PROTO, BOOT_UART_SEQ_END);
        return -1;
    }
    if (image_crc32(frame, 16U) != boot_le32(&frame[16])) {
        boot_uart_reply(1U, BOOT_UART_CODE_CRC, BOOT_UART_SEQ_END);
        return -1;
    }

    size = boot_le32(&frame[12]);
    if (size == 0U) {
        boot_uart_reply(1U, BOOT_UART_CODE_SIZE, BOOT_UART_SEQ_END);
        return -1;
    }

    /* 握手成功：擦除 OTA 并进入接收态 */
    memset(&info, 0, sizeof(info));
    info.version = boot_le32(&frame[8]);
    info.size = size;
    if (upgrade_begin(&info) != 0) {
        boot_uart_reply(1U, BOOT_UART_CODE_SIZE, BOOT_UART_SEQ_END);
        return -1;
    }
    boot_uart_reply(0U, BOOT_UART_CODE_OK, BOOT_UART_SEQ_END);

    if (boot_uart_receive_image(size) != 0) {
        upgrade_abort();
        return -1;
    }

    /* 结束帧收齐后做整镜像校验，再置位升级标志 */
    if (upgrade_verify() != 0) {
        upgrade_abort();
        boot_uart_reply(1U, BOOT_UART_CODE_VERIFY, BOOT_UART_SEQ_END);
        return -1;
    }
    (void)upgrade_finish();
    boot_request_upgrade();
    boot_uart_reply(0U, BOOT_UART_CODE_OK, BOOT_UART_SEQ_END);

    LOG_I(BOOT_TAG, "uart upgrade done, %u bytes", (unsigned)size);
    return 0;
}
