#ifndef TEA5767_REGISTER_H
#define TEA5767_REGISTER_H

/* NXP TEA5767 单芯片 FM 收音机寄存器（I2C 0x60）定义 */

#define TEA5767_ADDRESS_DEFAULT         0x60U
#define TEA5767_REG_COUNT               5U

/* 写寄存器字节 0 */
#define TEA5767_B0_MUTE                 0x80U
#define TEA5767_B0_SEARCH_MODE          0x40U
#define TEA5767_B0_PLL_HI_MASK          0x3FU

/* 写寄存器字节 1：PLL 低 8 位 */
#define TEA5767_B1_PLL_MASK             0xFFU

/* 写寄存器字节 2 */
#define TEA5767_B2_SEARCH_UP            0x80U
#define TEA5767_B2_SSL_MASK             0x60U
#define TEA5767_B2_HLSI                 0x10U  /* 1: 日本频段 76~91MHz，0: 87.5~108MHz */
#define TEA5767_B2_MONO                 0x08U  /* 1: 强制单声道 */
#define TEA5767_B2_MUTE_RIGHT           0x04U
#define TEA5767_B2_MUTE_LEFT            0x02U
#define TEA5767_B2_SWP1                 0x01U

/* 写寄存器字节 3 */
#define TEA5767_B3_STANDBY              0x40U
#define TEA5767_B4_DEEMPH_75US          0x08U  /* 1: 75us（美国），0: 50us（欧洲） */

/* 读寄存器位 */
#define TEA5767_R0_READY                0x80U
#define TEA5767_R0_BAND                 0x40U
#define TEA5767_R2_STEREO               0x80U

/* 频段与 PLL 换算：f(Hz) = PLL * 32768 / 4 - 225000 */
#define TEA5767_PLL_DIVIDER             8192.0f
#define TEA5767_PLL_OFFSET_HZ           225000.0f
#define TEA5767_FREQ_MIN_MHZ            87.5f
#define TEA5767_FREQ_MAX_MHZ            108.0f
#define TEA5767_FREQ_JAPAN_MIN_MHZ      76.0f
#define TEA5767_FREQ_JAPAN_MAX_MHZ      91.0f

/* 搜索与就绪 */
#define TEA5767_SEARCH_TIMEOUT_MS       5000U
#define TEA5767_SEARCH_POLL_MS          50U
#define TEA5767_IF_COUNT_GOOD           0x2EU /* IF 计数器居中值，搜索时判断停机 */

#endif /* TEA5767_REGISTER_H */
