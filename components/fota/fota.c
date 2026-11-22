/*
 * fota.c - FOTA 会话管理
 *
 * 只做参数校验与进度跟踪，Flash 操作全部转交板级 board_ota.c：
 *   begin  -> board_ota_begin（擦除 OTA 区）
 *   write  -> board_ota_write（按偏移写入原始 bin）
 *   finish -> board_ota_finish（校验并更新启动状态）
 *   abort  -> board_ota_abort（清除未完成的 ota 标识）
 */

#include "fota.h"

#include <stddef.h>

#if defined(__GNUC__)
#define FOTA_WEAK   __attribute__((weak))
#else
#define FOTA_WEAK
#endif

/*
 * 板级后端缺省实现：没有 board_ota.c 的构建（如主机演示目标）
 * 调用时统一返回错误，避免链接失败。
 */
FOTA_WEAK int board_ota_begin(uint32_t image_size, uint32_t image_crc)
{
    (void)image_size;
    (void)image_crc;
    return FOTA_ERR;
}

FOTA_WEAK int board_ota_write(uint32_t offset, const void *data, uint32_t len)
{
    (void)offset;
    (void)data;
    (void)len;
    return FOTA_ERR;
}

FOTA_WEAK int board_ota_finish(void)
{
    return FOTA_ERR;
}

FOTA_WEAK void board_ota_abort(void)
{
}

FOTA_WEAK int board_ota_state(fota_state_t *state)
{
    (void)state;
    return FOTA_ERR;
}

static bool     s_busy;
static uint32_t s_size;
static uint32_t s_written;

void fota_abort(void)
{
    if (s_busy) {
        board_ota_abort();
    }
    s_busy = false;
    s_written = 0U;
}

int fota_begin(uint32_t image_size, uint32_t image_crc)
{
    if (s_busy || image_size == 0U) {
        return FOTA_ERR;
    }
    if (board_ota_begin(image_size, image_crc) != FOTA_OK) {
        return FOTA_ERR;
    }
    s_busy = true;
    s_size = image_size;
    s_written = 0U;
    return FOTA_OK;
}

int fota_write(uint32_t offset, const void *data, uint32_t len)
{
    if (!s_busy || data == NULL || len == 0U) {
        return FOTA_ERR;
    }
    if (offset != s_written || len > (s_size - s_written)) {
        return FOTA_ERR;
    }
    if (board_ota_write(offset, data, len) != FOTA_OK) {
        return FOTA_ERR;
    }
    s_written += len;
    return FOTA_OK;
}

int fota_finish(void)
{
    int rc;

    if (!s_busy || s_written != s_size) {
        return FOTA_ERR;
    }
    rc = board_ota_finish();
    s_busy = false;
    return (rc == FOTA_OK) ? FOTA_OK : FOTA_ERR;
}

int fota_get_state(fota_state_t *state)
{
    if (state == NULL) {
        return FOTA_ERR;
    }
    return board_ota_state(state);
}

int fota_run(uint32_t image_size, uint32_t image_crc,
             fota_read_fn read, void *user)
{
    uint8_t buf[256];
    uint32_t offset = 0U;
    int n;

    if (read == NULL || image_size == 0U) {
        return FOTA_ERR;
    }
    if (fota_begin(image_size, image_crc) != FOTA_OK) {
        return FOTA_ERR;
    }

    while (offset < image_size) {
        uint32_t max = (uint32_t)sizeof(buf);

        if ((image_size - offset) < max) {
            max = image_size - offset;
        }
        n = read(buf, max, user);
        if (n <= 0 || (uint32_t)n > max) {
            fota_abort();
            return FOTA_ERR;
        }
        if (fota_write(offset, buf, (uint32_t)n) != FOTA_OK) {
            fota_abort();
            return FOTA_ERR;
        }
        offset += (uint32_t)n;
    }
    return fota_finish();
}
