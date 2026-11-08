#ifndef ISD17XX_H
#define ISD17XX_H

/*
 * ISD17xx（ISD1760 / ISD1790 / ISD17120）语音录放驱动（SPI）。
 *
 * 参考 libdriver 风格：
 *   isd17xx_init / isd17xx_deinit / isd17xx_read / isd17xx_write
 *   isd17xx_play / isd17xx_record / isd17xx_erase / isd17xx_stop
 *
 * 说明：
 *   - 芯片内部录音存储与播放无需 MCU 提供音频数据，SPI 只发命令。
 *   - 命令表、状态位与地址范围需按 datasheet/固件补充确认。
 *   - 录音/播放前需先 isd17xx_power_up，长时间不使用时 isd17xx_power_down。
 */

#include "common/driver_io.h"
#include "isd17xx_register.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    driver_spi_t spi;
    uint8_t device_id;
    bool powered;
    bool initialized;
} isd17xx_handle_t;

/**
 * @brief  初始化：CS 配置、上电、读取器件 ID
 * @param  spi  已初始化的 SPI 主机句柄
 * @param  cs   片选引脚，0 表示由 SPI 外设内部管理
 */
int isd17xx_init(isd17xx_handle_t *handle, hal_spi_t *spi, hal_pin_t cs);

int isd17xx_deinit(isd17xx_handle_t *handle);

/** 读取状态寄存器（完成标志 / 满 / 空 / 命令错误） */
int isd17xx_read(isd17xx_handle_t *handle, uint16_t *status);

/** 透传一条原始命令（cmd 后可选 address，MSB first） */
int isd17xx_write(isd17xx_handle_t *handle, uint8_t cmd, uint16_t address);

/** 上电 / 掉电 */
int isd17xx_power_up(isd17xx_handle_t *handle);
int isd17xx_power_down(isd17xx_handle_t *handle);

/** 播放指定地址开始的语音 */
int isd17xx_play(isd17xx_handle_t *handle, uint16_t address);

/** 从指定地址开始录音 */
int isd17xx_record(isd17xx_handle_t *handle, uint16_t address);

/** 擦除指定地址所在的一段 */
int isd17xx_erase(isd17xx_handle_t *handle, uint16_t address);

/** 全片擦除 */
int isd17xx_erase_all(isd17xx_handle_t *handle);

/** 停止当前录音/播放 */
int isd17xx_stop(isd17xx_handle_t *handle);

/** 等待命令执行完成（READY 置位） */
int isd17xx_wait_ready(isd17xx_handle_t *handle, uint32_t timeout_ms);

/** 查询是否处于空闲（READY） */
int isd17xx_is_ready(isd17xx_handle_t *handle, bool *ready);

#ifdef __cplusplus
}
#endif

#endif /* ISD17XX_H */
