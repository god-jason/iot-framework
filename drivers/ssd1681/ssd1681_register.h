#ifndef SSD1681_REGISTER_H
#define SSD1681_REGISTER_H

/*
 * SSD1681 电子墨水屏控制器命令定义（SPI，200x200，1bpp）。
 * 该控制器用于 GDEW0154Z04 / GDEY0154 等 1.54 寸 200x200 面板；
 * 初始化序列、LUT 与刷新模式需按具体面板调整。
 */

#define SSD1681_WIDTH               200U
#define SSD1681_HEIGHT              200U
#define SSD1681_WIDTH_BYTES         ((SSD1681_WIDTH + 7U) / 8U)     /* 25 */
#define SSD1681_GRAM_SIZE           (SSD1681_WIDTH_BYTES * SSD1681_HEIGHT)

/* 命令 */
#define SSD1681_CMD_DRIVER_OUTPUT   0x01U
#define SSD1681_CMD_DEEP_SLEEP      0x10U
#define SSD1681_CMD_DATA_ENTRY      0x11U
#define SSD1681_CMD_SW_RESET        0x12U
#define SSD1681_CMD_TEMP_SENSOR     0x18U
#define SSD1681_CMD_MASTER_ACT      0x20U
#define SSD1681_CMD_DISP_UPDATE1    0x21U
#define SSD1681_CMD_DISP_UPDATE2    0x22U
#define SSD1681_CMD_WRITE_RAM_BW    0x24U
#define SSD1681_CMD_WRITE_RAM_RED   0x26U
#define SSD1681_CMD_WRITE_VCOM      0x2CU
#define SSD1681_CMD_WRITE_LUT       0x32U
#define SSD1681_CMD_WRITE_BORDER    0x3CU
#define SSD1681_CMD_SET_RAM_X       0x44U
#define SSD1681_CMD_SET_RAM_Y       0x45U
#define SSD1681_CMD_AUTO_WRITE_RED  0x46U
#define SSD1681_CMD_AUTO_WRITE_BW   0x47U
#define SSD1681_CMD_SET_RAM_X_CNT   0x4EU
#define SSD1681_CMD_SET_RAM_Y_CNT   0x4FU
#define SSD1681_CMD_NOP             0x7FU

/* 参数：需按面板调整 */
#define SSD1681_GATE_COUNT_L       ((uint8_t)((SSD1681_HEIGHT - 1U) & 0xFFU))
#define SSD1681_GATE_COUNT_H       ((uint8_t)(((SSD1681_HEIGHT - 1U) >> 8) & 0x01U))
#define SSD1681_DATA_ENTRY_MODE    0x03U     /* X 自增，Y 自增，地址计数器更新 */
#define SSD1681_BORDER_WAVEFORM    0x05U
#define SSD1681_TEMP_SENSOR_VAL    0x80U     /* 使用内建温度传感器 */
#define SSD1681_UPDATE_SEQ_FULL    0xF7U     /* 全屏刷新：时钟/模拟/LUT/显示 */
#define SSD1681_UPDATE_SEQ_PARTIAL 0xFFU     /* 局部刷新（若面板支持） */
#define SSD1681_DEEP_SLEEP_MODE1   0x01U

/* 时序（ms） */
#define SSD1681_RESET_LOW_MS       10U
#define SSD1681_RESET_WAIT_MS      10U
#define SSD1681_BUSY_TIMEOUT_MS    4000U

#endif /* SSD1681_REGISTER_H */
