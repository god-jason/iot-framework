#ifndef ISD17XX_REGISTER_H
#define ISD17XX_REGISTER_H

/*
 * Winbond ISD17xx（ISD1760/ISD1790 等）语音录放芯片 SPI 命令定义。
 *
 * 时序：CS 拉低 -> 发送命令字节（+ 可选 2 字节地址，MSB first）->
 *       在后续字节期间从 MISO 读回状态 -> CS 拉高。
 *
 * 注意：命令表与状态位需按 datasheet/固件确认，下列取值为常用默认。
 */

/* 单字节命令 */
#define ISD17XX_CMD_NOP                 0x00U
#define ISD17XX_CMD_POWER_UP            0x01U
#define ISD17XX_CMD_STOP                0x02U
#define ISD17XX_CMD_SETREC              0x03U
#define ISD17XX_CMD_SETPLAY             0x04U
#define ISD17XX_CMD_SETERASE            0x05U
#define ISD17XX_CMD_PLAY_NEXT           0x06U
#define ISD17XX_CMD_G_ERASE             0x08U
#define ISD17XX_CMD_POWER_DOWN          0x0AU

/* 三字节命令（命令 + 16 位地址） */
#define ISD17XX_CMD_PLAY_ADDR           0x40U
#define ISD17XX_CMD_REC_ADDR            0x50U
#define ISD17XX_CMD_ERASE_ADDR          0x60U
#define ISD17XX_CMD_G_ERASE_ADDR        0x70U

/* 状态读取命令（读回 2 字节状态） */
#define ISD17XX_CMD_RD_STATUS           0x09U
#define ISD17XX_CMD_RD_DEVID            0x0AU

/* 状态位（SR，需按 datasheet 确认） */
#define ISD17XX_SR_CMD_ERR              0x0001U
#define ISD17XX_SR_FULL                 0x0002U
#define ISD17XX_SR_EMPTY                0x0004U
#define ISD17XX_SR_INT                  0x0008U
#define ISD17XX_SR_POWER_UP             0x0010U
#define ISD17XX_SR_READY                0x0100U

/* 器件 ID：ISD1760 / ISD1790 / ISD17120 等 */
#define ISD17XX_DEVID_1760              0xA0U
#define ISD17XX_DEVID_1790              0xA1U
#define ISD17XX_DEVID_17120             0xA2U

/* 录音/播放的地址边界（按型号不同，需按 datasheet/固件确认） */
#define ISD17XX_MAX_ADDRESS             0x03FFU

/* 超时与延时（ms） */
#define ISD17XX_POWER_UP_MS             50U
#define ISD17XX_CMD_READY_MS            25U
#define ISD17XX_ERASE_TIMEOUT_MS        6000U
#define ISD17XX_OP_TIMEOUT_MS           30000U
#define ISD17XX_POLL_MS                 10U
#define ISD17XX_POWER_DOWN_MS           10U

#endif /* ISD17XX_REGISTER_H */
