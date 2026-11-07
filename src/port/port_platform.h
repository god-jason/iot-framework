#ifndef PORT_PLATFORM_H
#define PORT_PLATFORM_H

/*
 * 平台注册接口。
 * 每个平台（ports/<platform>/）实现 port_platform_install()，
 * 在其中调用 port_platform_register() 注册 Platform Ops。
 * port_init() 会在尚未注册时自动调用 port_platform_install()。
 *
 * 生命周期：注册表只有一个全局槽位，后注册者覆盖先注册者；
 * 注册的 ops 必须是静态常量，调用者在整个运行期都不可释放其内存。
 * Ops 中的函数指针允许为 NULL，表示该平台未实现对应能力，
 * 由平台接口统一转换为 PORT_NOT_SUPPORTED 或 PORT_OK。
 */

#include "port_def.h"

#ifdef __cplusplus
extern "C" {
#endif

/* MCU 家族标识，用于上层做按家族的差异化处理 */
typedef enum {
    PORT_MCU_UNKNOWN = 0,       /* 未知/未识别 */
    PORT_MCU_STM32F1,           /* STM32F1 系列 */
    PORT_MCU_STM32F4,           /* STM32F4 系列 */
    PORT_MCU_STM32H7,           /* STM32H7 系列 */
    PORT_MCU_GD32,              /* 兆易创新 GD32 */
    PORT_MCU_CH32,              /* 沁恒 CH32 */
    PORT_MCU_N32,               /* 国民技术 N32 */
    PORT_MCU_AVR,               /* Atmel AVR */
    PORT_MCU_SIMULATOR          /* 主机模拟器 */
} port_mcu_family_t;

/* 芯片信息，由平台 get_info 填充 */
typedef struct {
    port_mcu_family_t family;   /* MCU 家族 */

    uint32_t model;             /* 型号标识（平台自定义编码） */
    uint32_t revision;          /* 芯片修订版本 */

    uint32_t core_clock;        /* 内核时钟频率（Hz） */
    uint32_t ahb_clock;         /* AHB 总线频率（Hz） */
    uint32_t apb1_clock;        /* APB1 总线频率（Hz） */
    uint32_t apb2_clock;        /* APB2 总线频率（Hz） */

    uint32_t flash_size;        /* 片内 Flash 容量（字节） */
    uint32_t sram_size;         /* 片内 SRAM 容量（字节） */
} port_mcu_info_t;

/*
 * 平台操作集合。
 * 所有函数在成功时返回 PORT_OK，失败时返回负值错误码。
 */
typedef struct {
    const char *name;           /* 平台名称，用于日志与 port_platform_name() */

    port_mcu_family_t family;   /* 平台所属 MCU 家族 */

    /* 平台初始化；NULL 视为无需初始化 */
    port_status_t (*init)(void);
    /* 平台反初始化；NULL 视为无需处理 */
    port_status_t (*deinit)(void);

    /* 获取芯片信息；info 由调用者提供且不可为 NULL */
    port_status_t (*get_info)(port_mcu_info_t *info);
} port_platform_ops_t;

/* 平台注册 / 获取 */
/*
 * 注册平台 Ops。
 * ops : 指向静态生命周期的操作表，不可为 NULL。
 * 返回: PORT_OK 成功；ops 为 NULL 返回 PORT_INVALID_PARAM。
 * 注意: 重复注册会直接覆盖，不做冲突检测。
 */
port_status_t port_platform_register(const port_platform_ops_t *ops);

/*
 * 获取已注册的平台 Ops。
 * 返回: 已注册的指针；未注册时返回 NULL。
 */
const port_platform_ops_t *port_platform_get(void);

/*
 * 获取平台名称。
 * 返回: 平台 name；未注册或 name 为 NULL 时返回字符串 "unknown"。
 */
const char *port_platform_name(void);

/*
 * 由平台实现：注册自身 Platform Ops。
 * 所有目标必须链接且仅链接一个平台实现。
 * 返回: PORT_OK 表示注册成功；其余为平台相关错误。
 */
port_status_t port_platform_install(void);

/*
 * 初始化 / 反初始化 PORT 层。
 * port_init() 在平台尚未注册时会自动调用 port_platform_install()。
 * 返回: 平台 install/init 的错误码；未注册成功返回 PORT_NOT_FOUND。
 */
port_status_t port_init(void);
port_status_t port_deinit(void);

/* 获取芯片信息；未注册平台返回 PORT_NOT_READY */
port_status_t port_get_info(port_mcu_info_t *info);

/* 能力查询：按设备类型 + 逻辑实例号返回 PORT_CAP_* 位掩码 */
typedef enum {
    PORT_DEVICE_GPIO = 0,       /* GPIO */
    PORT_DEVICE_UART,           /* UART/USART */
    PORT_DEVICE_SPI,            /* SPI */
    PORT_DEVICE_I2C,            /* I2C */
    PORT_DEVICE_CAN,            /* CAN */
    PORT_DEVICE_ADC,            /* ADC */
    PORT_DEVICE_DAC,            /* DAC */
    PORT_DEVICE_PWM,            /* PWM */
    PORT_DEVICE_TIMER,          /* 定时器 */
    PORT_DEVICE_FLASH,          /* 片内 Flash */
    PORT_DEVICE_USB,            /* USB */
    PORT_DEVICE_ETH             /* 以太网 */
} port_device_type_t;

/*
 * 查询外设能力。
 * type     : 设备类型；
 * instance : 逻辑实例号，从 0 开始。
 * 返回     : PORT_CAP_* 的位掩码；平台未注册或实例不存在时返回 0。
 */
uint32_t port_get_capabilities(port_device_type_t type, uint8_t instance);

#ifdef __cplusplus
}
#endif

#endif /* PORT_PLATFORM_H */
