#ifndef ST7789_REGISTER_H
#define ST7789_REGISTER_H

/*
 * ST7789 / ST7789V 命令定义（SPI，240x320，RGB565）。
 * 初始化序列为通用值，PORCTRL/GAMMA 等参数需按面板调整。
 */

#define ST7789_WIDTH                240U
#define ST7789_HEIGHT               320U
#define ST7789_GRAM_SIZE            (ST7789_WIDTH * ST7789_HEIGHT * 2U)

/* 命令 */
#define ST7789_CMD_NOP              0x00U
#define ST7789_CMD_SWRESET          0x01U
#define ST7789_CMD_SLPIN            0x10U
#define ST7789_CMD_SLPOUT           0x11U
#define ST7789_CMD_PTLON            0x12U
#define ST7789_CMD_NORON            0x13U
#define ST7789_CMD_INVOFF           0x20U
#define ST7789_CMD_INVON            0x21U
#define ST7789_CMD_DISPOFF          0x28U
#define ST7789_CMD_DISPON           0x29U
#define ST7789_CMD_CASET            0x2AU
#define ST7789_CMD_RASET            0x2BU
#define ST7789_CMD_RAMWR            0x2CU
#define ST7789_CMD_RAMRD            0x2EU
#define ST7789_CMD_MADCTL           0x36U
#define ST7789_CMD_COLMOD           0x3AU
#define ST7789_CMD_PORCTRL          0xB2U
#define ST7789_CMD_GCTRL            0xB7U
#define ST7789_CMD_VCOMS            0xBBU
#define ST7789_CMD_LCMCTRL          0xC0U
#define ST7789_CMD_VDVVRHEN         0xC2U
#define ST7789_CMD_VRHS             0xC3U
#define ST7789_CMD_VDVS             0xC4U
#define ST7789_CMD_FRCTRL2          0xC6U
#define ST7789_CMD_PWCTRL1          0xD0U
#define ST7789_CMD_PVGAMCTRL        0xE0U
#define ST7789_CMD_NVGAMCTRL        0xE1U

/* MADCTL 位 */
#define ST7789_MADCTL_MY            0x80U
#define ST7789_MADCTL_MX            0x40U
#define ST7789_MADCTL_MV            0x20U
#define ST7789_MADCTL_ML            0x10U
#define ST7789_MADCTL_BGR           0x08U

/* COLMOD：16 位 RGB565 */
#define ST7789_COLMOD_RGB565        0x55U

/* 常用颜色 */
#define ST7789_COLOR_BLACK          0x0000U
#define ST7789_COLOR_WHITE          0xFFFFU
#define ST7789_COLOR_RED            0xF800U
#define ST7789_COLOR_GREEN          0x07E0U
#define ST7789_COLOR_BLUE           0x001FU

/* 时序（ms）：参数需按面板调整 */
#define ST7789_RESET_LOW_MS         10U
#define ST7789_RESET_WAIT_MS        120U
#define ST7789_SLPOUT_MS            120U
#define ST7789_DISPON_MS            20U

/* 初始化参数表：以下数值为通用值，需按面板调整 */
#define ST7789_PORCTRL_INIT         { 0x0CU, 0x0CU, 0x00U, 0x33U, 0x33U }
#define ST7789_PWCTRL1_INIT         { 0xA4U, 0xA1U }
#define ST7789_PGAMMA_INIT          { \
        0xD0U, 0x08U, 0x11U, 0x08U, 0x0CU, 0x15U, 0x39U, \
        0x33U, 0x50U, 0x36U, 0x13U, 0x14U, 0x29U, 0x2DU }
#define ST7789_NGAMMA_INIT          { \
        0xD0U, 0x08U, 0x10U, 0x08U, 0x06U, 0x06U, 0x39U, \
        0x44U, 0x51U, 0x0BU, 0x16U, 0x14U, 0x2FU, 0x31U }

#endif /* ST7789_REGISTER_H */
