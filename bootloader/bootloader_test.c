/*
 * bootloader_test.c - Bootloader / 升级 / 镜像校验回归测试
 *
 * host 端通过 platforms/pc 的 RAM 模拟 Flash 运行，覆盖：
 * 分区读写擦除、镜像头与 CRC 校验、升级会话全流程、启动标志与安装流程、
 * 串口升级协议（握手、块重传、序号异常）。
 */

#include "test.h"
#include "bootloader.h"

#include <string.h>

#define TEST_IMAGE_PAYLOAD 64U

static uint8_t s_image[sizeof(image_header_t) + TEST_IMAGE_PAYLOAD];

/* 构造一个合法镜像：头 + 负载 + CRC */
static void build_image(uint32_t version, uint8_t seed)
{
    image_header_t header;
    uint32_t i;

    for (i = 0U; i < TEST_IMAGE_PAYLOAD; i++) {
        s_image[sizeof(image_header_t) + i] = (uint8_t)(seed + i);
    }

    memset(&header, 0, sizeof(header));
    header.magic = IMAGE_MAGIC;
    header.header_size = (uint16_t)sizeof(image_header_t);
    header.header_version = 1U;
    header.product_id = 0x1234U;
    header.hardware_id = 0x5678U;
    header.version = version;
    header.image_size = TEST_IMAGE_PAYLOAD;
    header.crc32 = image_crc32(&s_image[sizeof(image_header_t)],
                               TEST_IMAGE_PAYLOAD);

    memcpy(s_image, &header, sizeof(header));
}

static void write_partition_image(partition_type_t type)
{
    TF_CHECK_INT(partition_erase_all(type), 0);
    TF_CHECK_INT(partition_write(type, 0U, s_image, (uint32_t)sizeof(s_image)), 0);
}

static int s_jump_calls;

static void fake_jump(void)
{
    s_jump_calls++;
}

/* ---------------- 串口升级模拟端口 ---------------- */

static uint8_t  s_rx[2048];
static uint32_t s_rx_len;
static uint32_t s_rx_pos;
static uint8_t  s_tx[2048];
static uint32_t s_tx_len;

static int fake_read(uint8_t *buf, uint32_t len, uint32_t timeout_ms, void *user)
{
    uint32_t n = s_rx_len - s_rx_pos;

    (void)timeout_ms;
    (void)user;
    if (n == 0U) {
        return 0;                       /* 无数据：上层判为超时 */
    }
    if (n > len) {
        n = len;
    }
    memcpy(buf, &s_rx[s_rx_pos], n);
    s_rx_pos += n;
    return (int)n;
}

static int fake_write(const uint8_t *buf, uint32_t len, uint32_t timeout_ms,
                      void *user)
{
    (void)timeout_ms;
    (void)user;
    if (s_tx_len + len > sizeof(s_tx)) {
        return -1;
    }
    memcpy(&s_tx[s_tx_len], buf, len);
    s_tx_len += len;
    return (int)len;
}

static void rx_push(const void *data, uint32_t len)
{
    memcpy(&s_rx[s_rx_len], data, len);
    s_rx_len += len;
}

static void rx_u16(uint16_t v)
{
    s_rx[s_rx_len++] = (uint8_t)(v & 0xFFU);
    s_rx[s_rx_len++] = (uint8_t)(v >> 8);
}

static void rx_u32(uint32_t v)
{
    s_rx[s_rx_len++] = (uint8_t)(v & 0xFFU);
    s_rx[s_rx_len++] = (uint8_t)((v >> 8) & 0xFFU);
    s_rx[s_rx_len++] = (uint8_t)((v >> 16) & 0xFFU);
    s_rx[s_rx_len++] = (uint8_t)((v >> 24) & 0xFFU);
}

/* 请求帧：魔数 + 协议 + 版本 + 总长度 + CRC */
static void rx_request(uint32_t version, uint32_t size, bool bad_crc)
{
    uint8_t frame[BOOT_UART_REQ_SIZE];
    uint32_t crc;

    frame[0] = 'I';
    frame[1] = 'O';
    frame[2] = 'T';
    frame[3] = 'U';
    frame[4] = (uint8_t)BOOT_UART_PROTO;
    frame[5] = 0U;
    frame[6] = 0U;
    frame[7] = 0U;
    frame[8] = (uint8_t)(version & 0xFFU);
    frame[9] = (uint8_t)((version >> 8) & 0xFFU);
    frame[10] = (uint8_t)((version >> 16) & 0xFFU);
    frame[11] = (uint8_t)((version >> 24) & 0xFFU);
    frame[12] = (uint8_t)(size & 0xFFU);
    frame[13] = (uint8_t)((size >> 8) & 0xFFU);
    frame[14] = (uint8_t)((size >> 16) & 0xFFU);
    frame[15] = (uint8_t)((size >> 24) & 0xFFU);

    crc = image_crc32(frame, 16U);
    if (bad_crc) {
        crc ^= 0xFFFFFFFFU;
    }
    frame[16] = (uint8_t)(crc & 0xFFU);
    frame[17] = (uint8_t)((crc >> 8) & 0xFFU);
    frame[18] = (uint8_t)((crc >> 16) & 0xFFU);
    frame[19] = (uint8_t)((crc >> 24) & 0xFFU);

    rx_push(frame, sizeof(frame));
}

static void rx_block(uint16_t seq, const uint8_t *data, uint16_t len,
                     bool bad_crc)
{
    uint32_t crc = image_crc32(data, len);

    if (bad_crc) {
        crc ^= 0xFFFFFFFFU;
    }
    rx_u16(seq);
    rx_u16(len);
    rx_u32(crc);
    rx_push(data, len);
}

static void rx_end(void)
{
    rx_u16(BOOT_UART_SEQ_END);
    rx_u16(0U);
    rx_u32(0U);
}

void ut_bootloader(void)
{
    const partition_t *ota;
    const partition_t *app;
    image_header_t header;
    image_state_t state;
    upgrade_info_t info;
    upgrade_status_t status;
    uint8_t readback[16];

    tf_begin("bootloader/分区表");

    TF_CHECK_INT(boot_init(), 0);
    ota = partition_get(PART_OTA);
    app = partition_get(PART_APP);
    TF_CHECK(ota != NULL);
    TF_CHECK(app != NULL);
    if (ota != NULL) {
        TF_CHECK(ota->size > 0U);
        TF_CHECK(ota->writable);
    }
    TF_CHECK(partition_get(PART_LOG) == NULL);

    /* 越界分区拒绝注册 */
    {
        partition_t bad;

        memset(&bad, 0, sizeof(bad));
        bad.type = PART_LOG;
        bad.address = 0xFFFFF000U;
        bad.size = 0x1000U;
        TF_CHECK_INT(partition_register(&bad), -1);
    }

    tf_end();

    tf_begin("bootloader/分区读写擦除");

    TF_CHECK_INT(partition_erase_all(PART_OTA), 0);
    TF_CHECK_INT(partition_read(PART_OTA, 0U, readback, 4U), 0);
    TF_CHECK_INT(readback[0], 0xFF);            /* 擦除后为全 1 */

    TF_CHECK_INT(partition_write(PART_OTA, 0U, "ABCD", 4U), 0);
    TF_CHECK_INT(partition_read(PART_OTA, 0U, readback, 4U), 0);
    TF_CHECK_INT(readback[0], 'A');
    TF_CHECK_INT(readback[3], 'D');

    /* 越界读写被拒绝 */
    TF_CHECK_INT(partition_read(PART_OTA, ota->size, readback, 4U), -1);
    TF_CHECK_INT(partition_write(PART_OTA, ota->size - 2U, "XY", 4U), -1);

    tf_end();

    tf_begin("bootloader/镜像解析与 CRC");

    TF_CHECK(image_crc32("123456789", 9U) == 0xCBF43926U);   /* CRC-32 标准向量 */

    build_image(0x0102U, 0x10U);
    TF_CHECK_INT(image_parse(s_image, (uint32_t)sizeof(s_image), &header), 0);
    TF_CHECK_INT(image_verify(&header), 0);
    TF_CHECK_INT(image_get_state(&state), 0);
    TF_CHECK_INT(state, IMAGE_VALID);

    /* 篡改一个负载字节后 CRC 必须失败 */
    write_partition_image(PART_OTA);
    TF_CHECK_INT(image_verify_stored(PART_OTA), 0);
    s_image[sizeof(image_header_t) + 3U] ^= 0xFFU;
    TF_CHECK_INT(partition_write(PART_OTA, 0U, s_image,
                                 (uint32_t)sizeof(s_image)), 0);
    TF_CHECK_INT(image_verify_stored(PART_OTA), -1);
    TF_CHECK_INT(image_get_state(&state), 0);
    TF_CHECK_INT(state, IMAGE_INVALID);

    /* magic 错误直接判非法 */
    build_image(0x0102U, 0x10U);
    header.magic = 0U;
    TF_CHECK_INT(image_verify(&header), -1);

    tf_end();

    tf_begin("bootloader/升级流程");

    build_image(0x0200U, 0x20U);
    TF_CHECK_INT(upgrade_init(), 0);

    memset(&info, 0, sizeof(info));
    info.version = 0x0200U;
    info.size = (uint32_t)sizeof(s_image);
    TF_CHECK_INT(upgrade_begin(&info), 0);

    TF_CHECK_INT(upgrade_write(0U, s_image, (uint32_t)sizeof(s_image)), 0);
    TF_CHECK_INT(upgrade_verify(), 0);
    TF_CHECK_INT(upgrade_finish(), 0);
    TF_CHECK_INT(upgrade_get_status(&status), 0);
    TF_CHECK_INT(status.state, UPGRADE_SUCCESS);
    TF_CHECK_INT(status.received_size, (int)sizeof(s_image));

    /* 尺寸超过 OTA 分区时拒绝开始 */
    TF_CHECK_INT(upgrade_init(), 0);
    info.size = ota->size + 1U;
    TF_CHECK_INT(upgrade_begin(&info), -1);

    tf_end();

    tf_begin("bootloader/启动与安装");

    /* 有合法 OTA 镜像时选择升级模式并安装到 APP */
    build_image(0x0300U, 0x30U);
    write_partition_image(PART_OTA);

    TF_CHECK_INT(boot_init(), 0);
    TF_CHECK_INT(boot_select_image(), 0);
    TF_CHECK_INT(boot_get_mode(), BOOT_UPGRADE);
    TF_CHECK_INT(boot_verify_image(), 0);
    TF_CHECK_INT(boot_install_image(), 0);

    /* 安装后 APP 分区能独立通过 CRC 校验，且内容一致 */
    TF_CHECK_INT(image_verify_stored(PART_APP), 0);
    TF_CHECK_INT(partition_read(PART_APP, 0U, readback, 4U), 0);
    TF_CHECK_INT(readback[0], (int)(IMAGE_MAGIC & 0xFFU));

    /* APP 镜像损坏时启动选择应回落到 OTA */
    TF_CHECK_INT(partition_write(PART_APP, sizeof(image_header_t) + 1U,
                                 "X", 1U), 0);
    TF_CHECK_INT(image_verify_stored(PART_APP), -1);

    /* 跳转：host 通过钩子注入，未注入时无固件可跳转 */
    s_jump_calls = 0;
    TF_CHECK_INT(boot_set_jump(fake_jump), 0);
    TF_CHECK_INT(boot_start_application(), 0);
    TF_CHECK_INT(s_jump_calls, 1);

    /* 确认固件后回到普通启动模式 */
    TF_CHECK_INT(boot_confirm_application(), 0);
    TF_CHECK_INT(boot_get_mode(), BOOT_NORMAL);

    tf_end();

    tf_begin("bootloader/串口升级");

    boot_set_uart(fake_read, fake_write, NULL);

    /* 1) 没有请求：等待超时后返回 1，正常启动 */
    s_rx_len = 0U;
    s_rx_pos = 0U;
    s_tx_len = 0U;
    TF_CHECK_INT(boot_uart_wait_update(5U), 1);
    TF_CHECK_INT((int)s_tx_len, 0);

    /* 2) 握手帧 CRC 错误：回 NAK 并拒绝升级 */
    s_rx_len = 0U;
    s_rx_pos = 0U;
    s_tx_len = 0U;
    rx_request(1U, 64U, true);
    TF_CHECK_INT(boot_uart_wait_update(5U), -1);
    TF_CHECK((int)s_tx_len >= (int)BOOT_UART_REPLY_SIZE);
    TF_CHECK_INT(s_tx[4], 1);
    TF_CHECK_INT(s_tx[5], BOOT_UART_CODE_CRC);

    /* 3) 完整升级：首块 CRC 错 -> NAK，重发后继续，结束帧触发整镜像校验 */
    build_image(0x0400U, 0x40U);
    s_rx_len = 0U;
    s_rx_pos = 0U;
    s_tx_len = 0U;

    rx_request(0x0400U, (uint32_t)sizeof(s_image), false);
    rx_block(0U, s_image, 32U, true);
    rx_block(0U, s_image, 32U, false);
    rx_block(1U, &s_image[32], 32U, false);
    rx_block(2U, &s_image[64], (uint16_t)(sizeof(s_image) - 64U), false);
    rx_end();

    TF_CHECK_INT(boot_uart_wait_update(5U), 0);
    TF_CHECK_INT(image_verify_stored(PART_OTA), 0);
    TF_CHECK_INT(boot_get_mode(), BOOT_UPGRADE);
    /* 握手 ACK + 坏块 NAK + 3 个块 ACK + 结束 ACK */
    TF_CHECK_INT((int)(s_tx_len / BOOT_UART_REPLY_SIZE), 6);
    TF_CHECK_INT(s_tx[4], 0);
    TF_CHECK_INT(s_tx[s_tx_len - BOOT_UART_REPLY_SIZE + 4U], 0);
    TF_CHECK_INT(s_tx[s_tx_len - BOOT_UART_REPLY_SIZE + 5U],
                 BOOT_UART_CODE_OK);

    /* 4) 块序号跳跃：NAK 并中止本次升级 */
    s_rx_len = 0U;
    s_rx_pos = 0U;
    s_tx_len = 0U;
    rx_request(0x0401U, (uint32_t)sizeof(s_image), false);
    rx_block(1U, s_image, (uint16_t)sizeof(s_image), false);
    TF_CHECK_INT(boot_uart_wait_update(5U), -1);
    TF_CHECK_INT(s_tx[s_tx_len - BOOT_UART_REPLY_SIZE + 5U],
                 BOOT_UART_CODE_SEQ);

    tf_end();
}
