#ifndef WM8978_REGISTER_H
#define WM8978_REGISTER_H

/*
 * Wolfson WM8978 音频编解码器寄存器定义。
 *
 * 控制接口：I2C（7 位地址 0x1A），每个寄存器为 16 位控制字：
 *   bit15..bit9 = 寄存器地址，bit8..bit0 = 数据
 * 即线上为 2 字节大端：(addr << 1) | (data >> 8)，然后 data & 0xFF。
 * PCM 数据通路为 I2S，由调用方通过 port_i2s 配置与收发。
 */

#define WM8978_ADDRESS_DEFAULT          0x1AU

/* 寄存器地址 */
#define WM8978_REG_LOUT1                0x00U  /* 左耳机输出音量 */
#define WM8978_REG_ROUT1                0x01U  /* 右耳机输出音量 */
#define WM8978_REG_LOUT2                0x02U  /* 左扬声器输出音量 */
#define WM8978_REG_ROUT2                0x03U  /* 右扬声器输出音量 */
#define WM8978_REG_LOUTMIX              0x04U  /* 左输出混音 */
#define WM8978_REG_ROUTMIX              0x05U  /* 右输出混音 */
#define WM8978_REG_PM3                  0x06U  /* 电源管理 3（输出级） */
#define WM8978_REG_PM2                  0x07U  /* 电源管理 2（DAC/ADC） */
#define WM8978_REG_PM1                  0x08U  /* 电源管理 1（输入级） */
#define WM8978_REG_AUDIO_IFACE          0x09U  /* 音频接口格式 */
#define WM8978_REG_COMPANDING           0x0AU
#define WM8978_REG_CLOCKING             0x0BU  /* 采样率 / 主从模式 */
#define WM8978_REG_ADDITIONAL           0x0CU
#define WM8978_REG_GPIO                 0x0DU
#define WM8978_REG_JACKDETECT           0x0EU
#define WM8978_REG_DAC_CTRL             0x0FU
#define WM8978_REG_DAC_VOL_L            0x10U
#define WM8978_REG_DAC_VOL_R            0x11U
#define WM8978_REG_ADC_VOL_L            0x15U
#define WM8978_REG_ADC_VOL_R            0x16U
#define WM8978_REG_3D                   0x1BU
#define WM8978_REG_INPUT_MIX            0x2CU
#define WM8978_REG_LMUX                 0x2DU  /* 左 ADC 输入选择 */
#define WM8978_REG_RMUX                 0x2EU
#define WM8978_REG_ALC1                 0x31U
#define WM8978_REG_ALC2                 0x32U
#define WM8978_REG_ALC3                 0x33U
#define WM8978_REG_NOISE_GATE           0x34U
#define WM8978_REG_PLL_N                0x35U
#define WM8978_REG_PLL_K1               0x36U
#define WM8978_REG_PLL_K2               0x37U
#define WM8978_REG_PLL_K3               0x38U
#define WM8978_REG_COUNT                0x40U  /* 影子寄存器数量 */

/* 通用位 */
#define WM8978_VOL_UPDATE               0x0100U /* 音量寄存器 bit8：更新生效 */
#define WM8978_VOL_MUTE                 0x0040U /* bit6：静音 */
#define WM8978_VOL_MASK                 0x003FU /* bit5..0：音量码 */

/* 音频接口（R9） */
#define WM8978_IFACE_FMT_I2S            0x0040U /* bit6:5 = 10 I2S */
#define WM8978_IFACE_FMT_LEFT_JUST      0x0020U
#define WM8978_IFACE_FMT_RIGHT_JUST     0x0000U
#define WM8978_IFACE_WL_16BIT           0x0000U
#define WM8978_IFACE_WL_20BIT           0x0008U
#define WM8978_IFACE_WL_24BIT           0x0010U
#define WM8978_IFACE_WL_32BIT           0x0018U
#define WM8978_IFACE_MASTER             0x0001U

/* 电源管理位（常用组合，需按 datasheet/固件确认） */
#define WM8978_PM3_LOUT1                0x0001U
#define WM8978_PM3_ROUT1                0x0002U
#define WM8978_PM3_LOUT2                0x0004U
#define WM8978_PM3_ROUT2                0x0008U
#define WM8978_PM3_SPK                  0x0010U
#define WM8978_PM2_DACL                 0x0001U
#define WM8978_PM2_DACR                 0x0002U
#define WM8978_PM2_ADCL                 0x0004U
#define WM8978_PM2_ADCR                 0x0008U
#define WM8978_PM1_MICB                 0x0001U
#define WM8978_PM1_BIAS                 0x0002U
#define WM8978_PM1_BUFIO                0x0004U
#define WM8978_PM1_INPPGA               0x0008U

/* 输出混音：DAC 接入输出混音器 */
#define WM8978_MIX_DAC_TO_OUT           0x0050U

/* 播放通路默认值（需按 datasheet/固件确认） */
#define WM8978_DEF_LOUT1                0x01B0U /* 0dB，更新位 */
#define WM8978_DEF_ROUT1                0x01B0U
#define WM8978_DEF_LOUT2                0x01B0U
#define WM8978_DEF_ROUT2                0x01B0U
#define WM8978_DEF_DAC_VOL              0x01FFU /* 0dB 数字音量 */
#define WM8978_DEF_IFACE                (WM8978_IFACE_FMT_I2S | WM8978_IFACE_WL_16BIT)
#define WM8978_DEF_PM1                  0x0000U

/* 采样率（R11 编码，需按 datasheet/固件确认） */
#define WM8978_SR_8K                    0x0000U
#define WM8978_SR_12K                   0x0008U
#define WM8978_SR_16K                   0x0010U
#define WM8978_SR_24K                   0x0018U
#define WM8978_SR_32K                   0x0020U
#define WM8978_SR_44K1                  0x0032U
#define WM8978_SR_48K                   0x0038U
#define WM8978_SR_96K                   0x003CU

/* 延时（ms） */
#define WM8978_POWER_UP_MS              10U
#define WM8978_CONFIG_DELAY_MS          1U

#endif /* WM8978_REGISTER_H */
