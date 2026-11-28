#ifndef FOTA_H
#define FOTA_H

/*
 * fota.h - FOTA 升级会话接口
 *
 * fota_xxx 只做会话管理（开始 / 连续写入 / 完成 / 放弃），
 * OTA 区擦写与包头写入由端口层实现（STM32 见 platforms/stm32/stm32_ota.c）：
 *
 *   fota_begin  -> port_ota_begin
 *   fota_write  -> port_ota_write
 *   fota_finish -> port_ota_finish
 *   fota_abort  -> port_ota_abort
 *   fota_get_state -> port_ota_state
 *
 * 典型流程：
 *   fota_begin(size, crc32);
 *   while (...) fota_write(offset, data, n);
 *   fota_finish();        // 后端校验后写入 OTA 包头，重启后由 Bootloader 应用
 *
 * 约束：
 *   - 同一时刻只允许一个升级会话；
 *   - fota_write 的 offset 必须与已写字节数连续，且总长不超过 begin 的长度；
 *   - fota_finish 前必须写完 begin 指定的全部字节。
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "crc32.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FOTA_OK     0
#define FOTA_ERR    (-1)

/* ==========================================================================
 * OTA 升级包格式（写入 OTA 区的完整内容，Bootloader 与上位机共用）
 *
 *   +--------------------------+ 0x00
 *   | fota_hdr_t（32 字节）     |
 *   +--------------------------+ 0x20
 *   | 负载：data_len 字节       |
 *   +--------------------------+
 *
 * flags = 0       负载就是按 APP 区链接的原始镜像；
 * flags = DEFLATE 负载为 RAW DEFLATE（RFC1951）数据，解压后为 image_len。
 *
 * 头里同时记录「镜像」和「负载」的长度/CRC：
 *   - data_crc 校验 OTA 区里的实际字节，Bootloader 在擦除 APP 前先验证；
 *   - image_crc 校验最终写入 APP 区的镜像，升级后再验证一次；
 *   - hdr_crc 让包头自身可校验，magic/长度合法即表示 OTA 区存在待升级包。
 * ========================================================================== */

#define FOTA_HDR_MAGIC      0x41544F46U     /* 'FOTA'（小端字节：46 4F 54 41） */
#define FOTA_HDR_VERSION    1U
#define FOTA_HDR_SIZE       32U
#define FOTA_FLAG_DEFLATE   0x0001U         /* 负载为 RAW DEFLATE 压缩数据 */

typedef struct {
    uint32_t magic;         /* FOTA_HDR_MAGIC */
    uint16_t version;       /* FOTA_HDR_VERSION */
    uint16_t flags;         /* FOTA_FLAG_* */
    uint32_t image_len;     /* 升级后镜像长度（压缩时为解压后长度） */
    uint32_t image_crc;     /* 镜像 CRC32 */
    uint32_t data_len;      /* 负载长度（压缩时为压缩数据长度） */
    uint32_t data_crc;      /* 负载 CRC32 */
    uint32_t reserved;      /* 保留（写 0） */
    uint32_t hdr_crc;       /* 以上 28 字节的 CRC32 */
} fota_hdr_t;

typedef char fota_hdr_size_check[(sizeof(fota_hdr_t) == FOTA_HDR_SIZE) ? 1 : -1];

static inline uint32_t fota_hdr_crc(const fota_hdr_t *h)
{
    return crc32(h, FOTA_HDR_SIZE - 4U);
}

/* 填好 magic/version 并计算头 CRC（flags 及长度/CRC 字段需调用前填好） */
static inline void fota_hdr_seal(fota_hdr_t *h)
{
    h->magic = FOTA_HDR_MAGIC;
    h->version = FOTA_HDR_VERSION;
    h->reserved = 0U;
    h->hdr_crc = fota_hdr_crc(h);
}

/*
 * 包头结构校验（不访问负载，也不检查板级分区上限）：
 * 魔术字/版本/头 CRC/flags 合法，长度非零，未压缩时 data_len == image_len。
 */
static inline bool fota_hdr_valid(const fota_hdr_t *h)
{
    if (h == NULL) {
        return false;
    }
    if (h->magic != FOTA_HDR_MAGIC || h->version != FOTA_HDR_VERSION) {
        return false;
    }
    if (h->hdr_crc != fota_hdr_crc(h)) {
        return false;
    }
    if ((h->flags & (uint16_t)~FOTA_FLAG_DEFLATE) != 0U) {
        return false;
    }
    if (h->image_len == 0U || h->data_len == 0U) {
        return false;
    }
    if ((h->flags & FOTA_FLAG_DEFLATE) == 0U &&
        h->data_len != h->image_len) {
        return false;
    }
    return true;
}

/* 当前 OTA 区状态（由板级实现读取包头得到） */
typedef struct {
    bool     ota_valid;     /* OTA 区存在完整升级包（包头有效且负载校验通过） */
    uint32_t ota_len;       /* 升级包镜像长度 */
    uint32_t ota_crc;       /* 升级包镜像 CRC32 */
} fota_state_t;

/* ==========================================================================
 * 会话接口（组件层实现）
 * ========================================================================== */

int fota_begin(uint32_t image_size, uint32_t image_crc);
int fota_write(uint32_t offset, const void *data, uint32_t len);
int fota_finish(void);
void fota_abort(void);

/* 读取启动状态；参数为空返回 FOTA_ERR */
int fota_get_state(fota_state_t *state);

/*
 * 固件读取回调：把数据写入 buf，最多 max 字节。
 * 返回实际字节数，0 表示数据源结束，<0 表示错误。
 */
typedef int (*fota_read_fn)(uint8_t *buf, uint32_t max, void *user);

/*
 * 便捷流程：从 read 回调拉取 image_size 字节写入 OTA 区并完成校验/标记。
 * 不负责复位，调用者在成功后自行重启。
 */
int fota_run(uint32_t image_size, uint32_t image_crc,
             fota_read_fn read, void *user);

/* ==========================================================================
 * 板级后端（platforms/stm32/stm32_ota.c 实现）
 *
 * 未提供后端的构建会落到 fota.c 中的 weak 默认实现（恒返回 FOTA_ERR），
 * 因此主机侧其它目标无需链接板级代码。
 * ========================================================================== */

int  port_ota_begin(uint32_t image_size, uint32_t image_crc);
int  port_ota_write(uint32_t offset, const void *data, uint32_t len);
int  port_ota_finish(void);
void port_ota_abort(void);
int  port_ota_state(fota_state_t *state);

#ifdef __cplusplus
}
#endif

#endif /* FOTA_H */
