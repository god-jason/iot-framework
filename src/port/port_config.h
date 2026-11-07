#ifndef PORT_CONFIG_H
#define PORT_CONFIG_H

/*
 * PORT 功能裁剪开关。
 * 平台实现可据此选择编译哪些模块；被关闭的模块仍保留头文件，
 * 调用时返回 PORT_NOT_SUPPORTED。
 */

#ifndef PORT_USING_GPIO
#define PORT_USING_GPIO       1
#endif
#ifndef PORT_USING_EXTI
#define PORT_USING_EXTI       1
#endif
#ifndef PORT_USING_DMA
#define PORT_USING_DMA        1
#endif

#ifndef PORT_USING_UART
#define PORT_USING_UART       1
#endif
#ifndef PORT_USING_SPI
#define PORT_USING_SPI        1
#endif
#ifndef PORT_USING_I2C
#define PORT_USING_I2C        1
#endif
#ifndef PORT_USING_I2S
#define PORT_USING_I2S        1
#endif
#ifndef PORT_USING_CAN
#define PORT_USING_CAN        1
#endif

#ifndef PORT_USING_ADC
#define PORT_USING_ADC        1
#endif
#ifndef PORT_USING_DAC
#define PORT_USING_DAC        1
#endif
#ifndef PORT_USING_COMP
#define PORT_USING_COMP       1
#endif
#ifndef PORT_USING_OPAMP
#define PORT_USING_OPAMP      1
#endif

#ifndef PORT_USING_TIM
#define PORT_USING_TIM        1
#endif
#ifndef PORT_USING_PWM
#define PORT_USING_PWM        1
#endif
#ifndef PORT_USING_CAPTURE
#define PORT_USING_CAPTURE    1
#endif
#ifndef PORT_USING_ENCODER
#define PORT_USING_ENCODER    1
#endif

#ifndef PORT_USING_WDG
#define PORT_USING_WDG        1
#endif
#ifndef PORT_USING_RTC
#define PORT_USING_RTC        1
#endif
#ifndef PORT_USING_FLASH
#define PORT_USING_FLASH      1
#endif
#ifndef PORT_USING_CRC
#define PORT_USING_CRC        1
#endif
#ifndef PORT_USING_RNG
#define PORT_USING_RNG        1
#endif
#ifndef PORT_USING_PWR
#define PORT_USING_PWR        1
#endif

#ifndef PORT_USING_QSPI
#define PORT_USING_QSPI       1
#endif
#ifndef PORT_USING_SDIO
#define PORT_USING_SDIO       1
#endif
#ifndef PORT_USING_USB
#define PORT_USING_USB        1
#endif
#ifndef PORT_USING_ETH
#define PORT_USING_ETH        1
#endif

#endif /* PORT_CONFIG_H */
