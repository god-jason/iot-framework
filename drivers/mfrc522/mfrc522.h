#ifndef MFRC522_H
#define MFRC522_H

/*
 * MFRC522 13.56MHz RFID 读卡器驱动（SPI + RST）。
 *
 * 已实现：软复位、VersionReg 探测、天线/增益配置、REQA + 防冲突 + SELECT
 * 读取 UID、原始帧收发（可直接发送 MIFARE 读写块命令）。
 * 未实现：Crypto1 认证（扇区读写前必须由上层完成，或后续按 datasheet 补充）。
 */

#include "hal.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    hal_spi_dev_t spi;
    hal_pin_t reset;           /* 可传 0，则仅软件复位 */
    uint8_t version;
    uint32_t timeout_ms;
    bool initialized;
} mfrc522_handle_t;

typedef struct {
    uint8_t uid[10];
    uint8_t uid_len;
    uint8_t atqa[2];
    uint8_t sak;
    bool valid;
} mfrc522_card_t;

int mfrc522_init(mfrc522_handle_t *handle, hal_spi_t *spi,
                 hal_pin_t cs, hal_pin_t reset);
int mfrc522_deinit(mfrc522_handle_t *handle);

int mfrc522_get_version(mfrc522_handle_t *handle, uint8_t *version);
int mfrc522_set_antenna(mfrc522_handle_t *handle, bool on);
/* 接收增益 0..7，对应 RFCfgReg 的 RxGain 位 */
int mfrc522_set_gain(mfrc522_handle_t *handle, uint8_t rx_gain);

/* 寻卡并读取 UID（REQA + 防冲突 + SELECT），无卡返回 HAL_NOT_FOUND */
int mfrc522_read(mfrc522_handle_t *handle, mfrc522_card_t *card);

/* 发送原始 MIFARE 帧并返回应答（如 {0xA0, block, 16B} 写块，需已认证） */
int mfrc522_write(mfrc522_handle_t *handle, const uint8_t *frame, size_t len,
                  uint8_t *response, size_t response_size, size_t *response_len);
/* 让卡进入 HALT 状态 */
int mfrc522_halt(mfrc522_handle_t *handle);

#ifdef __cplusplus
}
#endif

#endif /* MFRC522_H */
