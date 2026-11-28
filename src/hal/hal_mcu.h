#ifndef HAL_MCU_H
#define HAL_MCU_H

/*
 * 平台/芯片信息、能力查询与系统级原语。
 *
 * 具体平台（platforms/<platform>/）实现 hal_init() / hal_deinit() /
 * hal_get_info() / hal_get_capabilities() 以及时基、延时、中断开关、
 * 复位与低功耗接口；所有目标仅链接一个平台实现。
 */

#include <stdint.h>

#include "hal_base.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------- 外设能力标志 ----------------
 * 位掩码形式，可按位或组合；由 hal_get_capabilities() 返回。
 */
typedef enum {
    HAL_CAP_DMA           = HAL_BIT(0),   /* 支持 DMA 传输 */
    HAL_CAP_IT            = HAL_BIT(1),   /* 支持中断方式传输 */
    HAL_CAP_CIRCULAR      = HAL_BIT(2),   /* 支持循环（环形）模式 */
    HAL_CAP_DOUBLE_BUFFER = HAL_BIT(3),   /* 支持双缓冲 */
    HAL_CAP_HALF_TRANSFER = HAL_BIT(4),   /* 支持半传输事件 */
    HAL_CAP_IDLE_DETECT   = HAL_BIT(5),   /* 支持总线空闲检测 */
    HAL_CAP_CACHE         = HAL_BIT(6),   /* 需要/支持缓存维护操作 */
    HAL_CAP_FIFO          = HAL_BIT(7)    /* 具备硬件 FIFO */
} hal_capability_e;

/* MCU 家族标识，用于上层做按家族的差异化处理 */
typedef enum {
    HAL_MCU_UNKNOWN = 0,       /* 未知/未识别 */
    HAL_MCU_STM32F1,           /* STM32F1 系列 */
    HAL_MCU_STM32F4,           /* STM32F4 系列 */
    HAL_MCU_STM32H7,           /* STM32H7 系列 */
    HAL_MCU_GD32,              /* 兆易创新 GD32 */
    HAL_MCU_CH32,              /* 沁恒 CH32 */
    HAL_MCU_N32,               /* 国民技术 N32 */
    HAL_MCU_AVR,               /* Atmel AVR */
    HAL_MCU_PC          /* 主机模拟器 */
} hal_mcu_family_e;

/* 芯片信息，由平台 hal_get_info() 填充 */
typedef struct {
    hal_mcu_family_e family;   /* MCU 家族 */

    uint32_t model;             /* 型号标识（平台自定义编码） */
    uint32_t revision;          /* 芯片修订版本 */

    uint32_t core_clock;        /* 内核时钟频率（Hz） */
    uint32_t ahb_clock;         /* AHB 总线频率（Hz） */
    uint32_t apb1_clock;        /* APB1 总线频率（Hz） */
    uint32_t apb2_clock;        /* APB2 总线频率（Hz） */

    uint32_t flash_size;        /* 片内 Flash 容量（字节） */
    uint32_t sram_size;         /* 片内 SRAM 容量（字节） */
} hal_mcu_info_t;

/* 设备类型，用于 hal_get_capabilities() 查询 */
typedef enum {
    HAL_DEVICE_GPIO = 0,       /* GPIO */
    HAL_DEVICE_UART,           /* UART/USART */
    HAL_DEVICE_SPI,            /* SPI */
    HAL_DEVICE_I2C,            /* I2C */
    HAL_DEVICE_CAN,            /* CAN */
    HAL_DEVICE_ADC,            /* ADC */
    HAL_DEVICE_DAC,            /* DAC */
    HAL_DEVICE_PWM,            /* PWM */
    HAL_DEVICE_TIMER,          /* 定时器 */
    HAL_DEVICE_FLASH,          /* 片内 Flash */
    HAL_DEVICE_USB,            /* USB */
    HAL_DEVICE_ETH             /* 以太网 */
} hal_device_type_e;

/*
 * 初始化 / 反初始化 PORT 层。
 * 由当前链接的平台实现提供。
 * 返回: HAL_OK 成功；其余为平台相关错误。
 */
int hal_init(void);
int hal_deinit(void);

/* 获取芯片信息；info 不可为 NULL */
int hal_get_info(hal_mcu_info_t *info);

/*
 * 查询外设能力。
 * type     : 设备类型；
 * instance : 逻辑实例号，从 0 开始。
 * 返回     : HAL_CAP_* 的位掩码；实例不存在时返回 0。
 */
uint32_t hal_get_capabilities(hal_device_type_e type, uint8_t instance);

/* ================= 系统级原语 =================
 *
 * 时基、中断开关、复位与低功耗，供 drivers / src / modules 使用。
 * 延时统一使用内核的 ros_sleep_ms()。
 * 具体实现位于 platforms/<platform>/：
 *   - simulator      : 纯软件推进 tick；复位/睡眠仅置标记，便于单元测试；
 *   - STM32F1/F4/H7  : HAL_GetTick / NVIC_SystemReset / PWR 低功耗；
 *   - AVR            : 定时器毫秒计数、看门狗复位、sleep 模式。
 *
 * 约定：
 *   - tick 为 32 位毫秒计数，允许自然回绕；判断超时应使用无符号差值，
 *     例如 (uint32_t)(hal_tick_get() - start) >= timeout；
 *   - hal_system_reset() 不返回；
 *   - hal_irq_disable()/hal_irq_enable() 不具备嵌套计数能力，
 *     只用于保护极短的临界区，进入与退出必须成对出现。
 */

/*
 * 获取系统毫秒时基。
 * 返回 : 自系统启动以来的毫秒数，允许回绕。
 * 注意 : 可在中断中调用；返回值只保证单调递增（回绕除外）。
 */
uint32_t hal_tick_get(void);

/*
 * 微秒级忙等延时。
 * 参数 : us 延时长度（微秒）。
 * 说明 : 纯轮询实现，不依赖 tick 中断，可在关中断的临界区内调用；
 *        用于单总线/位带类器件（DHT11/22、DS18B20、HC-SR04、IR）的位时序。
 *        不得用它替代 ros_sleep_ms() 做长延时（会独占 CPU）。
 * 注意 : 精度取决于平台时基（STM32 用 DWT 周期计数，AVR 用校准空循环，
 *        PC 模拟端直接推进模拟 tick）。
 */
void hal_delay_us(uint32_t us);

/* 保存并关闭全局中断，返回先前状态；与 hal_irq_restore() 成对使用。
 * 相比 hal_irq_disable()/enable()，本接口支持嵌套（恢复进入前的状态）。 */
uint32_t hal_irq_save(void);

/* 恢复 hal_irq_save() 保存的中断状态。 */
void hal_irq_restore(uint32_t state);

/*
 * 使能全局中断。
 * 注意 : 与 hal_irq_disable() 成对使用，不支持嵌套计数。
 */
void hal_irq_enable(void);

/*
 * 关闭全局中断。
 * 注意 : 临界区应尽量短；与 hal_irq_enable() 成对使用，不支持嵌套计数。
 */
void hal_irq_disable(void);

/*
 * 软件复位系统。
 * 注意 : 该函数不返回，调用前应保存需要跨复位保留的数据。
 */
void hal_system_reset(void);

/*
 * 进入浅睡眠（IDLE/SLEEP）模式。
 * 说明 : 内核停止，外设与时钟保持运行，任意中断均可唤醒。
 * 返回 : 被唤醒后返回。
 */
void hal_system_sleep(void);

/*
 * 进入深度睡眠（STOP/PWR_DOWN）模式。
 * 说明 : 关闭大部分时钟以降低功耗，唤醒源由平台/芯片决定。
 * 返回 : 被唤醒后返回。
 * 注意 : STM32 的 STOP 模式唤醒后系统时钟可能退回内部时钟，
 *        AVR 的 PWR_DOWN 仅保留部分唤醒源，调用者需按平台恢复时钟与外设。
 */
void hal_system_deepsleep(void);

#ifdef __cplusplus
}
#endif

#endif /* HAL_MCU_H */
