#ifndef LD3320_H
#define LD3320_H

/*
 * LD3320 非特定人语音识别芯片驱动（SPI 控制接口）。
 *
 * 参考 libdriver 风格：
 *   ld3320_init / ld3320_deinit / ld3320_read / ld3320_write
 *   ld3320_start / ld3320_stop / ld3320_get_result
 *
 * 说明：
 *   - SPI 采用 3 字节报文（CMD + ADDR + DATA），片选由 driver_spi_t 管理。
 *   - 完整的语音识别流程需要写入 DSP 固件与数十条寄存器序列（关键词表、
 *     麦克风增益、ADC/DAC 配置等），这些序列需按 datasheet/官方固件补充，
 *     本驱动只实现基础读写与启动/停止/取结果骨架。
 *   - 并口模式未实现，如需并口请使用调用方的 port GPIO 直接映射。
 */

#include "common/driver_io.h"
#include "ld3320_register.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    driver_spi_t spi;
    iot_pin_t reset_pin;   /* RST 引脚，0 表示不控制 */
    iot_pin_t irq_pin;     /* 中断/忙指示引脚，0 表示不接 */
    bool initialized;
} ld3320_handle_t;

/**
 * @brief  初始化 LD3320：复位、片选与基础寄存器检查
 * @param  spi       已初始化的 SPI 主机句柄
 * @param  cs        片选引脚，0 表示由 SPI 外设内部管理
 * @param  reset_pin 复位引脚，0 表示不控制
 */
int ld3320_init(ld3320_handle_t *handle, iot_spi_t *spi, iot_pin_t cs,
                iot_pin_t reset_pin);

int ld3320_deinit(ld3320_handle_t *handle);

/** 写寄存器（8 位地址 + 8 位数据） */
int ld3320_write(ld3320_handle_t *handle, uint8_t reg, uint8_t value);

/** 读寄存器 */
int ld3320_read(ld3320_handle_t *handle, uint8_t reg, uint8_t *value);

/** 连续写多个寄存器（按 reg, value 顺序给出的数组，count 为寄存器个数） */
int ld3320_write_regs(ld3320_handle_t *handle, const uint8_t *pairs, size_t count);

/** 启动语音识别（写入基础启动命令，DSP 序列需补充） */
int ld3320_start(ld3320_handle_t *handle);

/** 停止语音识别 */
int ld3320_stop(ld3320_handle_t *handle);

/** 查询识别结果：返回 0 表示尚无结果，index 为关键词索引 */
int ld3320_get_result(ld3320_handle_t *handle, uint8_t *index);

/** 开始播放提示音（基础路径，音源由上层通过 I2S/DAC 提供） */
int ld3320_play_start(ld3320_handle_t *handle);

/** 停止播放 */
int ld3320_play_stop(ld3320_handle_t *handle);

#ifdef __cplusplus
}
#endif

#endif /* LD3320_H */
