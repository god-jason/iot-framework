#ifndef PORT_CONFIG_H
#define PORT_CONFIG_H

/*
 * PORT 功能裁剪开关。
 * 平台实现可据此选择编译哪些模块；被关闭的模块仍保留头文件，
 * 调用时返回 PORT_NOT_SUPPORTED。
 *
 * 用法：所有开关均用 #ifndef 包裹，允许在编译选项或平台配置中
 * 预先定义以覆盖默认值。1 = 启用，0 = 裁剪。
 */

/* ---------------- GPIO / EXTI / DMA ---------------- */
#ifndef PORT_USING_GPIO
#define PORT_USING_GPIO       1      /* 通用输入输出 */
#endif
#ifndef PORT_USING_EXTI
#define PORT_USING_EXTI       1      /* 外部中断 */
#endif
#ifndef PORT_USING_DMA
#define PORT_USING_DMA        1      /* DMA 控制器 */
#endif

/* ---------------- 串行通信 ---------------- */
#ifndef PORT_USING_UART
#define PORT_USING_UART       1      /* 通用异步收发 */
#endif
#ifndef PORT_USING_SPI
#define PORT_USING_SPI        1      /* SPI 总线 */
#endif
#ifndef PORT_USING_I2C
#define PORT_USING_I2C        1      /* I2C 总线 */
#endif
#ifndef PORT_USING_I2S
#define PORT_USING_I2S        1      /* I2S 音频总线 */
#endif
#ifndef PORT_USING_CAN
#define PORT_USING_CAN        1      /* CAN / CAN-FD 总线 */
#endif

/* ---------------- 模拟外设 ---------------- */
#ifndef PORT_USING_ADC
#define PORT_USING_ADC        1      /* 模数转换 */
#endif
#ifndef PORT_USING_DAC
#define PORT_USING_DAC        1      /* 数模转换 */
#endif
#ifndef PORT_USING_COMP
#define PORT_USING_COMP       1      /* 模拟比较器 */
#endif
#ifndef PORT_USING_OPAMP
#define PORT_USING_OPAMP      1      /* 运算放大器 */
#endif

/* ---------------- 定时器类 ---------------- */
#ifndef PORT_USING_TIM
#define PORT_USING_TIM        1      /* 通用定时器 */
#endif
#ifndef PORT_USING_PWM
#define PORT_USING_PWM        1      /* PWM 输出 */
#endif
#ifndef PORT_USING_CAPTURE
#define PORT_USING_CAPTURE    1      /* 输入捕获 */
#endif
#ifndef PORT_USING_ENCODER
#define PORT_USING_ENCODER    1      /* 正交编码器接口 */
#endif

/* ---------------- 系统与存储 ---------------- */
#ifndef PORT_USING_WDG
#define PORT_USING_WDG        1      /* 看门狗 */
#endif
#ifndef PORT_USING_RTC
#define PORT_USING_RTC        1      /* 实时时钟 */
#endif
#ifndef PORT_USING_FLASH
#define PORT_USING_FLASH      1      /* 片内 Flash */
#endif
#ifndef PORT_USING_CRC
#define PORT_USING_CRC        1      /* CRC 计算单元 */
#endif
#ifndef PORT_USING_RNG
#define PORT_USING_RNG        1      /* 硬件随机数 */
#endif
#ifndef PORT_USING_PWR
#define PORT_USING_PWR        1      /* 电源与低功耗管理 */
#endif

/* ---------------- 高速/外部接口 ---------------- */
#ifndef PORT_USING_QSPI
#define PORT_USING_QSPI       1      /* 四线 SPI / 外部存储 */
#endif
#ifndef PORT_USING_SDIO
#define PORT_USING_SDIO       1      /* SD/MMC 主机接口 */
#endif
#ifndef PORT_USING_USB
#define PORT_USING_USB        1      /* USB 主机/设备 */
#endif
#ifndef PORT_USING_ETH
#define PORT_USING_ETH        1      /* 以太网 MAC */
#endif

#endif /* PORT_CONFIG_H */
