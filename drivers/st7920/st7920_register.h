#ifndef ST7920_REGISTER_H
#define ST7920_REGISTER_H

/*
 * ST7920 128x64 点阵 LCD 命令与时序定义（串行 3 线模式）。
 *
 * 串行帧格式：5 位同步头 11111 + RW + RS + 0，再跟高 4 位、低 4 位。
 * 因此每个字节需要 3 个 SPI 字节：帧头、高半字节、低半字节。
 */

#define ST7920_WIDTH                128U
#define ST7920_HEIGHT               64U
#define ST7920_WIDTH_BYTES          (ST7920_WIDTH / 8U)             /* 16 */
#define ST7920_GRAM_SIZE            (ST7920_HEIGHT * ST7920_WIDTH_BYTES)  /* 1024 */

/* 串行帧头 */
#define ST7920_SYNC_CMD             0xF8U   /* RW=0, RS=0 */
#define ST7920_SYNC_DATA            0xFAU   /* RW=0, RS=1 */

/* 基本指令集 */
#define ST7920_CMD_CLEAR            0x01U
#define ST7920_CMD_HOME             0x02U
#define ST7920_CMD_ENTRY_MODE       0x06U   /* 光标右移，地址加 1 */
#define ST7920_CMD_DISPLAY_OFF      0x08U
#define ST7920_CMD_DISPLAY_ON       0x0CU   /* 显示开，光标/闪烁关 */
#define ST7920_CMD_FUNCTION_BASIC   0x30U   /* 8 位接口，基本指令集 */
#define ST7920_CMD_FUNCTION_EXT     0x34U   /* 8 位接口，扩展指令集 */
#define ST7920_CMD_GRAPHIC_ON       0x36U   /* 扩展指令集：图形显示开 */
#define ST7920_CMD_GRAPHIC_OFF      0x34U   /* 扩展指令集：图形显示关 */

/* GDRAM 地址设置：0x80 | addr */
#define ST7920_CMD_SET_GDRAM_ADDR   0x80U

/* 上电/清屏延时（ms） */
#define ST7920_POWER_ON_MS          40U
#define ST7920_RESET_WAIT_MS        1U
#define ST7920_CLEAR_MS             12U

/*
 * GDRAM 映射说明：
 * 本驱动假定每个 GDRAM 单元为 8 列 x 2 行（垂直地址 v 与 v+32），
 * 即一次写入的两个数据字节分别落到行 v 与行 v+32 的连续 8 个像素。
 * 若面板显示出现错位/交错，需按实际面板调整该映射。
 */

#endif /* ST7920_REGISTER_H */
