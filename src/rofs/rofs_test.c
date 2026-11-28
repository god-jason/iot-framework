/*
 * rofs_test.c - ROFS 1.0 只读镜像测试
 * ============================================================================
 *
 * 在 RAM 中构造一个符合规范的镜像：
 *   config/device.json、config/net.json、readme.txt、www/index.html
 * 然后验证挂载、读取、目录遍历、路径校验与 CRC 校验。
 */

#include "test.h"
#include "rofs.h"
#include "rofs_internal.h"

#include <stdio.h>
#include <string.h>

#define T_IMG_MAX   512u

static uint8_t s_img[T_IMG_MAX];
static uint32_t s_img_len;
static uint32_t s_readme_body;
static uint32_t s_readme_size;

/* 小端写入辅助 */
static void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

/* 追加一条记录，返回新的偏移 */
static uint32_t put_record(uint32_t off, uint8_t type, const char *name,
                           const void *body, uint32_t size, uint32_t time)
{
    uint32_t nlen = (uint32_t)strlen(name);
    uint32_t crc = crc32(body, size);

    s_img[off + ROFS_REC_OFF_TYPE] = type;
    s_img[off + ROFS_REC_OFF_NAME_LEN] = (uint8_t)nlen;
    put_u16(&s_img[off + ROFS_REC_OFF_FLAGS], 0u);
    put_u32(&s_img[off + ROFS_REC_OFF_SIZE], size);
    put_u32(&s_img[off + ROFS_REC_OFF_TIME], time);
    put_u32(&s_img[off + ROFS_REC_OFF_CRC], crc);

    memcpy(&s_img[off + ROFS_RECORD_SIZE], name, nlen);
    if (size > 0u) {
        memcpy(&s_img[off + ROFS_RECORD_SIZE + nlen], body, size);
    }
    return off + ROFS_RECORD_SIZE + nlen + size;
}

/* 构造镜像：返回总长度，并输出 readme.txt 的 Body 偏移/大小 */
static uint32_t build_image(void)
{
    static const char b_device[] = "{\"a\":1}";
    static const char b_net[] = "{}";
    static const char b_readme[] = "hello";
    static const char b_html[] = "<html>";
    uint32_t off = ROFS_SUPER_SIZE;
    uint32_t rec_start = off;
    uint32_t crc;
    uint32_t total;

    memset(s_img, 0, sizeof(s_img));

    off = put_record(off, ROFS_TYPE_FILE, "config/device.json",
                     b_device, sizeof(b_device) - 1u, 1000u);
    off = put_record(off, ROFS_TYPE_FILE, "config/net.json",
                     b_net, sizeof(b_net) - 1u, 1001u);
    s_readme_body = off + ROFS_RECORD_SIZE + (uint32_t)strlen("readme.txt");
    s_readme_size = sizeof(b_readme) - 1u;
    off = put_record(off, ROFS_TYPE_FILE, "readme.txt",
                     b_readme, s_readme_size, 1002u);
    off = put_record(off, ROFS_TYPE_FILE, "www/index.html",
                     b_html, sizeof(b_html) - 1u, 1003u);

    /* End 标记 */
    put_u32(&s_img[off], ROFS_END_MAGIC);
    total = off + 4u;

    /* Super Header */
    put_u32(&s_img[ROFS_SB_OFF_MAGIC], ROFS_MAGIC);
    put_u16(&s_img[ROFS_SB_OFF_VERSION], ROFS_VERSION);
    put_u16(&s_img[ROFS_SB_OFF_HDR_SIZE], ROFS_SUPER_SIZE);
    put_u32(&s_img[ROFS_SB_OFF_IMAGE_SIZE], total);
    put_u32(&s_img[ROFS_SB_OFF_REC_COUNT], 4u);
    put_u32(&s_img[ROFS_SB_OFF_REC_OFFSET], rec_start);
    put_u32(&s_img[ROFS_SB_OFF_REC_SIZE], off - rec_start);
    put_u32(&s_img[ROFS_SB_OFF_IMAGE_CRC], 0u);
    put_u32(&s_img[ROFS_SB_OFF_FLAGS], 0u);

    /* 镜像 CRC：计算时 image_crc32 字段为 0 */
    crc = crc32(s_img, total);
    put_u32(&s_img[ROFS_SB_OFF_IMAGE_CRC], crc);

    s_img_len = total;
    return total;
}

static int img_read(void *ctx, uint32_t offset, void *buffer, uint32_t size)
{
    (void)ctx;
    if (offset > s_img_len || size > s_img_len - offset) {
        return -1;
    }
    memcpy(buffer, &s_img[offset], size);
    return 0;
}

void ut_rofs(void)
{
    static rofs_t fs;
    rofs_file_t f;
    rofs_dir_t d;
    rofs_dirent_t ent;
    rofs_stat_t st;
    rofs_info_t info;
    char buf[32];
    int rc;

    build_image();

    tf_begin("rofs/mount-info");
    TF_CHECK_INT(rofs_init(&fs), ROFS_OK);
    TF_CHECK_INT(rofs_mount(&fs, 0u, img_read, NULL), ROFS_OK);
    TF_CHECK_INT(rofs_get_info(&fs, &info), ROFS_OK);
    TF_CHECK_INT(info.version, ROFS_VERSION);
    TF_CHECK_INT(info.record_count, 4);
    TF_CHECK_INT(info.records_offset, (int)ROFS_SUPER_SIZE);
    TF_CHECK_INT(info.image_size, (int)s_img_len);
    TF_CHECK_INT(rofs_check(&fs), ROFS_OK);
    tf_end();

    tf_begin("rofs/file-read");
    TF_CHECK_INT(rofs_open(&fs, "readme.txt", &f), ROFS_OK);
    TF_CHECK_INT(rofs_size(&f), 5);
    TF_CHECK_INT(rofs_tell(&f), 0);
    memset(buf, 0, sizeof(buf));
    TF_CHECK_INT(rofs_read(&f, buf, sizeof(buf)), 5);
    TF_CHECK_STR(buf, "hello");
    TF_CHECK_INT(rofs_eof(&f), 1);
    TF_CHECK_INT(rofs_read(&f, buf, 1), 0);
    TF_CHECK_INT(rofs_seek(&f, 0, SEEK_SET), ROFS_OK);
    memset(buf, 0, sizeof(buf));
    TF_CHECK_INT(rofs_read(&f, buf, 2), 2);
    TF_CHECK_STR(buf, "he");
    TF_CHECK_INT(rofs_seek(&f, -1, SEEK_CUR), ROFS_OK);
    TF_CHECK_INT(rofs_tell(&f), 1);
    TF_CHECK_INT(rofs_seek(&f, 6, SEEK_SET), ROFS_ERR_RANGE);
    TF_CHECK_INT(rofs_close(&f), ROFS_OK);
    tf_end();

    tf_begin("rofs/stat-exists");
    TF_CHECK_INT(rofs_exists(&fs, "readme.txt"), 1);
    TF_CHECK_INT(rofs_exists(&fs, "missing.txt"), 0);
    TF_CHECK_INT(rofs_stat(&fs, "readme.txt", &st), ROFS_OK);
    TF_CHECK_STR(st.name, "readme.txt");
    TF_CHECK_INT(st.type, ROFS_TYPE_FILE);
    TF_CHECK_INT(st.size, 5);
    TF_CHECK_INT(rofs_open(&fs, "config", &f), ROFS_ERR_IS_DIR);
    TF_CHECK_INT(rofs_open(&fs, "missing.txt", &f), ROFS_ERR_NOT_FOUND);
    TF_CHECK_INT(rofs_open(&fs, "../x", &f), ROFS_ERR_INVALID);
    TF_CHECK_INT(rofs_open(&fs, "config/", &f), ROFS_ERR_INVALID);
    tf_end();

    tf_begin("rofs/dir-root");
    memset(&d, 0, sizeof(d));
    TF_CHECK_INT(rofs_opendir(&fs, "", &d), ROFS_OK);
    TF_CHECK_INT(rofs_readdir(&d, &ent), 0);
    TF_CHECK_STR(ent.name, "config");
    TF_CHECK_INT(ent.type, ROFS_TYPE_DIR);
    TF_CHECK_INT(rofs_readdir(&d, &ent), 0);
    TF_CHECK_STR(ent.name, "readme.txt");
    TF_CHECK_INT(ent.type, ROFS_TYPE_FILE);
    TF_CHECK_INT(ent.size, 5);
    TF_CHECK_INT(rofs_readdir(&d, &ent), 0);
    TF_CHECK_STR(ent.name, "www");
    TF_CHECK_INT(ent.type, ROFS_TYPE_DIR);
    TF_CHECK_INT(rofs_readdir(&d, &ent), 1);
    TF_CHECK_INT(rofs_rewinddir(&d), ROFS_OK);
    TF_CHECK_INT(rofs_readdir(&d, &ent), 0);
    TF_CHECK_STR(ent.name, "config");
    TF_CHECK_INT(rofs_closedir(&d), ROFS_OK);
    tf_end();

    tf_begin("rofs/dir-sub");
    memset(&d, 0, sizeof(d));
    TF_CHECK_INT(rofs_opendir(&fs, "config", &d), ROFS_OK);
    TF_CHECK_INT(rofs_readdir(&d, &ent), 0);
    TF_CHECK_STR(ent.name, "device.json");
    TF_CHECK_INT(rofs_readdir(&d, &ent), 0);
    TF_CHECK_STR(ent.name, "net.json");
    TF_CHECK_INT(rofs_readdir(&d, &ent), 1);
    TF_CHECK_INT(rofs_closedir(&d), ROFS_OK);

    memset(&d, 0, sizeof(d));
    TF_CHECK_INT(rofs_opendir(&fs, "/www", &d), ROFS_OK);
    TF_CHECK_INT(rofs_readdir(&d, &ent), 0);
    TF_CHECK_STR(ent.name, "index.html");
    TF_CHECK_INT(rofs_readdir(&d, &ent), 1);
    TF_CHECK_INT(rofs_closedir(&d), ROFS_OK);

    memset(&d, 0, sizeof(d));
    TF_CHECK_INT(rofs_opendir(&fs, "nope", &d), ROFS_ERR_NOT_FOUND);
    tf_end();

    tf_begin("rofs/corrupt");
    /* Body 字节破坏：记录 CRC 校验失败 */
    s_img[s_readme_body] ^= 0xFFu;
    TF_CHECK_INT(rofs_mount(&fs, 0u, img_read, NULL), ROFS_OK);
    TF_CHECK_INT(rofs_check(&fs), ROFS_ERR_CRC);
    s_img[s_readme_body] ^= 0xFFu;

    /* 魔数破坏 */
    s_img[0] ^= 0xFFu;
    TF_CHECK_INT(rofs_mount(&fs, 0u, img_read, NULL), ROFS_ERR_MAGIC);
    s_img[0] ^= 0xFFu;

    /* End 标记破坏 */
    s_img[s_img_len - 1u] ^= 0xFFu;
    TF_CHECK_INT(rofs_mount(&fs, 0u, img_read, NULL), ROFS_ERR_FORMAT);
    s_img[s_img_len - 1u] ^= 0xFFu;

    /* 恢复后仍可挂载 */
    TF_CHECK_INT(rofs_mount(&fs, 0u, img_read, NULL), ROFS_OK);
    rc = rofs_check(&fs);
    TF_CHECK_INT(rc, ROFS_OK);
    tf_end();
}
