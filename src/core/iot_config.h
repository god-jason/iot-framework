#ifndef IOT_CONFIG_H
#define IOT_CONFIG_H

/*
 * PORT 功能裁剪开关。
 * 平台实现可据此选择编译哪些模块；被关闭的模块仍保留头文件，
 * 调用时返回 IOT_NOT_SUPPORTED。
 *
 * 用法：所有开关均用 #ifndef 包裹，允许在编译选项或平台配置中
 * 预先定义以覆盖默认值。1 = 启用，0 = 裁剪。
 */

/* ---------------- GPIO / EXTI / DMA ---------------- */
#ifndef IOT_USING_GPIO
#define IOT_USING_GPIO       1      /* 通用输入输出 */
#endif
#ifndef IOT_USING_EXTI
#define IOT_USING_EXTI       1      /* 外部中断 */
#endif
#ifndef IOT_USING_DMA
#define IOT_USING_DMA        1      /* DMA 控制器 */
#endif

/* ---------------- 串行通信 ---------------- */
#ifndef IOT_USING_UART
#define IOT_USING_UART       1      /* 通用异步收发 */
#endif
#ifndef IOT_USING_SPI
#define IOT_USING_SPI        1      /* SPI 总线 */
#endif
#ifndef IOT_USING_I2C
#define IOT_USING_I2C        1      /* I2C 总线 */
#endif
#ifndef IOT_USING_I2S
#define IOT_USING_I2S        1      /* I2S 音频总线 */
#endif
#ifndef IOT_USING_CAN
#define IOT_USING_CAN        1      /* CAN / CAN-FD 总线 */
#endif

/* ---------------- 模拟外设 ---------------- */
#ifndef IOT_USING_ADC
#define IOT_USING_ADC        1      /* 模数转换 */
#endif
#ifndef IOT_USING_DAC
#define IOT_USING_DAC        1      /* 数模转换 */
#endif
#ifndef IOT_USING_OPAMP
#define IOT_USING_OPAMP      1      /* 运算放大器 */
#endif

/* ---------------- 定时器类 ---------------- */
#ifndef IOT_USING_TIM
#define IOT_USING_TIM        1      /* 通用定时器 */
#endif
#ifndef IOT_USING_PWM
#define IOT_USING_PWM        1      /* PWM 输出 */
#endif
#ifndef IOT_USING_CAPTURE
#define IOT_USING_CAPTURE    1      /* 输入捕获 */
#endif
#ifndef IOT_USING_ENCODER
#define IOT_USING_ENCODER    1      /* 正交编码器接口 */
#endif

/* ---------------- 系统与存储 ---------------- */
#ifndef IOT_USING_WDG
#define IOT_USING_WDG        1      /* 看门狗 */
#endif
#ifndef IOT_USING_RTC
#define IOT_USING_RTC        1      /* 实时时钟 */
#endif
#ifndef IOT_USING_FLASH
#define IOT_USING_FLASH      1      /* 片内 Flash */
#endif
#ifndef IOT_USING_CRC
#define IOT_USING_CRC        1      /* CRC 计算单元 */
#endif
#ifndef IOT_USING_RNG
#define IOT_USING_RNG        1      /* 硬件随机数 */
#endif
#ifndef IOT_USING_PWR
#define IOT_USING_PWR        1      /* 电源与低功耗管理 */
#endif

/* ---------------- 高速/外部接口 ---------------- */
#ifndef IOT_USING_QSPI
#define IOT_USING_QSPI       1      /* 四线 SPI / 外部存储 */
#endif
#ifndef IOT_USING_SDIO
#define IOT_USING_SDIO       1      /* SD/MMC 主机接口 */
#endif
#ifndef IOT_USING_USB
#define IOT_USING_USB        1      /* USB 主机/设备 */
#endif
#ifndef IOT_USING_ETH
#define IOT_USING_ETH        1      /* 以太网 MAC */
#endif

#endif /* IOT_CONFIG_H */
