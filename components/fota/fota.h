#ifndef FOTA_H
#define FOTA_H

/*
 * fota.h - FOTA 升级会话接口
 *
 * fota_xxx 只做会话管理（开始 / 连续写入 / 完成 / 放弃），
 * OTA 区擦写与启动状态更新由端口层实现（STM32 见 platforms/stm32/stm32_ota.c）：
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
 *   fota_finish();        // 后端校验并置 ota 标识，重启后由 Bootloader 应用
 *
 * 约束：
 *   - 同一时刻只允许一个升级会话；
 *   - fota_write 的 offset 必须与已写字节数连续，且总长不超过 begin 的长度；
 *   - fota_finish 前必须写完 begin 指定的全部字节。
 */

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FOTA_OK     0
#define FOTA_ERR    (-1)

/* 当前启动状态（由板级实现从 Meta 区读取） */
typedef struct {
    bool     ota_valid;     /* OTA 区存在完整升级包 */
    uint32_t ota_len;       /* 升级包长度 */
    uint32_t ota_crc;       /* 升级包 CRC32 */
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
