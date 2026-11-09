#if defined(IOT_STM32_H7)

/*
 * STM32H7 平台实现（新 PORT 硬件抽象层）。
 *
 * 本文件直接实现 hal 下的公共 API，并依赖 STM32H7 HAL
 * （stm32h7xx_hal.h）。每个目标只链接一个平台实现；host 不编译。
 *
 * 实例分配约定：
 *   - 多实例外设（UART/SPI/I2C/TIM/...）由本文件内部静态槽位池分配，
 *     按“首个空闲实例”顺序映射到固定基址表。调用者无需预先指定实例。
 *   - 单实例外设（RTC/RNG/WDG/QSPI/SDMMC/ETH）使用单例上下文。
 *   - 所有外设时钟默认由 CubeMX 生成的 SystemClock_Config/MX_xxx_Init
 *     打开；本文件只负责 GPIO/DMA 等必要的本地时钟控制，避免与 HAL
 *     初始化代码重复配置。
 */
#include "hal.h"

#include "stm32h7xx_hal.h"

#include <string.h>

/* ---------------- 配置常量 ---------------- */
#define STM32_DEVICE_MODEL         0x483U        /* STM32H743 */
#define STM32_CORE_CLOCK           480000000U
#define STM32_FLASH_SIZE           (2U * 1024U * 1024U)
#define STM32_SRAM_SIZE            (1U * 1024U * 1024U)
#define STM32_FLASH_BASE_ADDR      0x08000000UL
#define STM32_FLASH_SECTOR_SIZE    0x20000UL     /* 128 KB */
#define STM32_FLASH_BANK_SIZE      0x100000UL    /* 1 MB / bank */
#define STM32_FLASH_WORD_SIZE      32U

#define STM32_SLOT_MAX             64U
#define STM32_LINE_MAX             16U

/* ---------------- 内部类型 ---------------- */
typedef enum {
    STM32_SLOT_NONE = 0,
    STM32_SLOT_DMA,
    STM32_SLOT_UART,
    STM32_SLOT_SPI,
    STM32_SLOT_I2C,
    STM32_SLOT_I2S,
    STM32_SLOT_CAN,
    STM32_SLOT_ADC,
    STM32_SLOT_DAC,
    STM32_SLOT_COMP,
    STM32_SLOT_OPAMP,
    STM32_SLOT_TIM,
    STM32_SLOT_PWM,
    STM32_SLOT_CAPTURE,
    STM32_SLOT_ENCODER
} stm32_slot_kind_t;

typedef union {
    DMA_HandleTypeDef dma;
    UART_HandleTypeDef uart;
    SPI_HandleTypeDef spi;
    I2S_HandleTypeDef i2s;
    I2C_HandleTypeDef i2c;
    FDCAN_HandleTypeDef can;
    ADC_HandleTypeDef adc;
    DAC_HandleTypeDef dac;
    OPAMP_HandleTypeDef opamp;
    TIM_HandleTypeDef tim;          /* TIM / PWM / CAPTURE / ENCODER */
} stm32_hw_union_t;

typedef struct {
    stm32_slot_kind_t kind;
    uint8_t index;                  /* 设备实例号 */
    void *owner;                    /* 对应的公共 handle */
    uint32_t rx_length;             /* UART receive-to-idle 长度 */
    stm32_hw_union_t hw;
} stm32_slot_t;

/* ---------------- 基址表 ---------------- */
static GPIO_TypeDef *const s_gpio_ports[] = {
    GPIOA, GPIOB, GPIOC, GPIOD, GPIOE, GPIOF,
    GPIOG, GPIOH, GPIOI, GPIOJ, GPIOK
};

static USART_TypeDef *const s_uart_bases[] = {
    USART1, USART2, USART3, UART4, UART5, USART6, UART7, UART8
};

static SPI_TypeDef *const s_spi_bases[] = {
    SPI1, SPI2, SPI3, SPI4, SPI5, SPI6
};

static I2C_TypeDef *const s_i2c_bases[] = {
    I2C1, I2C2, I2C3, I2C4
};

static SPI_TypeDef *const s_i2s_bases[] = {
    SPI2, SPI3
};

static FDCAN_GlobalTypeDef *const s_can_bases[] = {
    FDCAN1, FDCAN2
};

static ADC_TypeDef *const s_adc_bases[] = {
    ADC1, ADC2, ADC3
};

static TIM_TypeDef *const s_tim_bases[] = {
    TIM1, TIM2, TIM3, TIM4, TIM5, TIM6,
    TIM7, TIM8, TIM12, TIM15, TIM16, TIM17
};

static DMA_Stream_TypeDef *const s_dma_streams[] = {
    DMA1_Stream0, DMA1_Stream1, DMA1_Stream2, DMA1_Stream3,
    DMA1_Stream4, DMA1_Stream5, DMA1_Stream6, DMA1_Stream7,
    DMA2_Stream0, DMA2_Stream1, DMA2_Stream2, DMA2_Stream3,
    DMA2_Stream4, DMA2_Stream5, DMA2_Stream6, DMA2_Stream7
};

/* ---------------- 槽位池 ---------------- */
static stm32_slot_t s_slots[STM32_SLOT_MAX];

/* ---------------- EXTI 回调表 ---------------- */
typedef struct {
    hal_exti_callback_t callback;
    void *user_data;
    hal_pin_t pin;
    bool enabled;
} stm32_exti_entry_t;

static stm32_exti_entry_t s_exti[STM32_LINE_MAX];

/* ---------------- 核心状态 ---------------- */

/* ---------------- 单例外设上下文 ---------------- */
static IWDG_HandleTypeDef s_iwdg;
static bool s_iwdg_initialized = false;

static RTC_HandleTypeDef s_rtc;
static bool s_rtc_initialized = false;
static hal_rtc_alarm_callback_t s_rtc_alarm_callback = NULL;
static void *s_rtc_alarm_user_data = NULL;

static CRC_HandleTypeDef s_crc;
static bool s_crc_initialized = false;

static RNG_HandleTypeDef s_rng;
static bool s_rng_initialized = false;

static QSPI_HandleTypeDef s_qspi;
static bool s_qspi_initialized = false;

static SD_HandleTypeDef s_sd;
static bool s_sd_initialized = false;

static ETH_HandleTypeDef s_eth;
static bool s_eth_initialized = false;

static bool s_usb_initialized = false;
static bool s_usb_connected = false;

/* ---------------- 通用辅助 ---------------- */
static int stm32_from_hal(HAL_StatusTypeDef status)
{
    switch (status) {
    case HAL_OK:
        return HAL_OK;
    case HAL_BUSY:
        return HAL_BUSY;
    case HAL_TIMEOUT:
        return HAL_TIMEOUT;
    default:
        return HAL_ERROR;
    }
}

static uint32_t stm32_timeout(uint32_t timeout)
{
    return (timeout == HAL_TIMEOUT_FOREVER) ? HAL_MAX_DELAY : timeout;
}

static int stm32_base_init(hal_handle_base_t *base, void *instance)
{
    if (base->state != HAL_STATE_RESET) {
        return HAL_ALREADY_INIT;
    }
    base->instance = instance;
    base->state = HAL_STATE_READY;
    base->error = HAL_OK;
    return HAL_OK;
}

static int stm32_base_check(const hal_handle_base_t *base)
{
    if (base->state == HAL_STATE_RESET) {
        return HAL_NOT_READY;
    }
    return HAL_OK;
}

static void stm32_base_reset(hal_handle_base_t *base)
{
    base->instance = NULL;
    base->state = HAL_STATE_RESET;
    base->error = HAL_ERROR;
}

static stm32_slot_t *stm32_slot_alloc(stm32_slot_kind_t kind, uint8_t max_instances,
                                uint8_t *index)
{
    uint32_t used = 0UL;
    stm32_slot_t *free_slot = NULL;
    uint8_t i;

    if (index == NULL || max_instances == 0U || max_instances > 32U) {
        return NULL;
    }
    for (i = 0U; i < STM32_SLOT_MAX; i++) {
        if (s_slots[i].kind == kind) {
            used |= (1UL << s_slots[i].index);
        } else if (s_slots[i].kind == STM32_SLOT_NONE && free_slot == NULL) {
            free_slot = &s_slots[i];
        }
    }
    for (i = 0U; i < max_instances; i++) {
        if ((used & (1UL << i)) == 0UL) {
            if (free_slot == NULL) {
                return NULL;
            }
            memset(free_slot, 0, sizeof(*free_slot));
            free_slot->kind = kind;
            free_slot->index = i;
            *index = i;
            return free_slot;
        }
    }
    return NULL;
}

static void stm32_slot_free(stm32_slot_t *slot)
{
    if (slot != NULL) {
        memset(slot, 0, sizeof(*slot));
    }
}

/* ---------------- Platform ---------------- */
int hal_init(void)
{
    return HAL_OK;
}

int hal_deinit(void)
{
    return HAL_OK;
}

int hal_get_info(hal_mcu_info_t *info)
{
    if (info == NULL) {
        return HAL_INVALID_PARAM;
    }
    memset(info, 0, sizeof(*info));
    info->family = HAL_MCU_STM32H7;
    info->model = STM32_DEVICE_MODEL;
    info->revision = 0U;
    info->core_clock = 480000000U;
    info->ahb_clock = 240000000U;
    info->apb1_clock = 120000000U;
    info->apb2_clock = 120000000U;
    info->flash_size = STM32_FLASH_SIZE;
    info->sram_size = STM32_SRAM_SIZE;
    return HAL_OK;
}

/* ---------------- 时钟 ---------------- */
int hal_clock_init(const hal_clock_config_t *config)
{
    if (config == NULL) {
        return HAL_INVALID_PARAM;
    }
    /* 时钟树由 CubeMX 的 SystemClock_Config() 配置，避免与 HAL 重复设置 PLL。 */
    return HAL_NOT_SUPPORTED;
}

uint32_t hal_clock_get_frequency(hal_clock_bus_e bus)
{
    switch (bus) {
    case HAL_CLOCK_CORE:
        return (uint32_t)HAL_RCC_GetSysClockFreq();
    case HAL_CLOCK_AHB:
        return (uint32_t)HAL_RCC_GetHCLKFreq();
    case HAL_CLOCK_APB1:
        return (uint32_t)HAL_RCC_GetPCLK1Freq();
    case HAL_CLOCK_APB2:
        return (uint32_t)HAL_RCC_GetPCLK2Freq();
    case HAL_CLOCK_APB3:
        return (uint32_t)HAL_RCC_GetPCLK1Freq();
    case HAL_CLOCK_APB4:
        return (uint32_t)HAL_RCC_GetPCLK1Freq();
    default:
        return 0U;
    }
}

int hal_clock_enable(void *peripheral)
{
    if (peripheral == NULL) {
        return HAL_INVALID_PARAM;
    }
    /* 外设门控时钟由 MX_xxx_Init() 中的 __HAL_RCC_xxx_CLK_ENABLE() 完成。 */
    return HAL_NOT_SUPPORTED;
}

int hal_clock_disable(void *peripheral)
{
    if (peripheral == NULL) {
        return HAL_INVALID_PARAM;
    }
    return HAL_NOT_SUPPORTED;
}

/* ---------------- GPIO ---------------- */
static GPIO_TypeDef *stm32_gpio_port(hal_pin_t pin)
{
    uint32_t port = HAL_PIN_GET_PORT(pin);
    if (port >= HAL_ARRAY_SIZE(s_gpio_ports)) {
        return NULL;
    }
    return s_gpio_ports[port];
}

static void stm32_gpio_clock_enable(uint32_t port)
{
    switch (port) {
    case 0U: __HAL_RCC_GPIOA_CLK_ENABLE(); break;
    case 1U: __HAL_RCC_GPIOB_CLK_ENABLE(); break;
    case 2U: __HAL_RCC_GPIOC_CLK_ENABLE(); break;
    case 3U: __HAL_RCC_GPIOD_CLK_ENABLE(); break;
    case 4U: __HAL_RCC_GPIOE_CLK_ENABLE(); break;
    case 5U: __HAL_RCC_GPIOF_CLK_ENABLE(); break;
    case 6U: __HAL_RCC_GPIOG_CLK_ENABLE(); break;
    case 7U: __HAL_RCC_GPIOH_CLK_ENABLE(); break;
    case 8U: __HAL_RCC_GPIOI_CLK_ENABLE(); break;
    case 9U: __HAL_RCC_GPIOJ_CLK_ENABLE(); break;
    case 10U: __HAL_RCC_GPIOK_CLK_ENABLE(); break;
    default: break;
    }
}

static uint32_t stm32_gpio_speed(hal_gpio_speed_e speed)
{
    switch (speed) {
    case HAL_GPIO_SPEED_LOW:       return GPIO_SPEED_FREQ_LOW;
    case HAL_GPIO_SPEED_MEDIUM:    return GPIO_SPEED_FREQ_MEDIUM;
    case HAL_GPIO_SPEED_HIGH:      return GPIO_SPEED_FREQ_HIGH;
    case HAL_GPIO_SPEED_VERY_HIGH: return GPIO_SPEED_FREQ_VERY_HIGH;
    default:                        return GPIO_SPEED_FREQ_LOW;
    }
}

static uint32_t stm32_gpio_pull(hal_gpio_pull_e pull)
{
    switch (pull) {
    case HAL_GPIO_PULLUP:   return GPIO_PULLUP;
    case HAL_GPIO_PULLDOWN: return GPIO_PULLDOWN;
    default:                 return GPIO_NOPULL;
    }
}

static bool stm32_pin_valid(hal_pin_t pin)
{
    return (HAL_PIN_GET_PORT(pin) < HAL_ARRAY_SIZE(s_gpio_ports)) &&
           (HAL_PIN_GET_INDEX(pin) < 16U);
}

int hal_gpio_init(hal_pin_t pin, const hal_gpio_config_t *config)
{
    GPIO_InitTypeDef init;
    GPIO_TypeDef *port;

    if (config == NULL || !stm32_pin_valid(pin)) {
        return HAL_INVALID_PARAM;
    }
    port = stm32_gpio_port(pin);
    stm32_gpio_clock_enable(HAL_PIN_GET_PORT(pin));

    memset(&init, 0, sizeof(init));
    init.Pin = (uint32_t)(1UL << HAL_PIN_GET_INDEX(pin));
    init.Pull = stm32_gpio_pull(config->pull);
    init.Speed = stm32_gpio_speed(config->speed);
    init.Alternate = config->alternate;

    switch (config->mode) {
    case HAL_GPIO_MODE_INPUT:
        init.Mode = GPIO_MODE_INPUT;
        break;
    case HAL_GPIO_MODE_OUTPUT:
        /* H7 GPIO 不支持独立开漏位，统一使用推挽输出。 */
        init.Mode = GPIO_MODE_OUTPUT_PP;
        break;
    case HAL_GPIO_MODE_ANALOG:
        init.Mode = GPIO_MODE_ANALOG;
        break;
    case HAL_GPIO_MODE_AF:
        init.Mode = GPIO_MODE_AF_PP;
        break;
    default:
        return HAL_INVALID_PARAM;
    }

    HAL_GPIO_Init(port, &init);
    if (config->mode == HAL_GPIO_MODE_OUTPUT) {
        HAL_GPIO_WritePin(port, (uint16_t)init.Pin,
                          (config->initial_level == HAL_GPIO_HIGH) ? GPIO_PIN_SET
                                                                    : GPIO_PIN_RESET);
    }
    return HAL_OK;
}

int hal_gpio_deinit(hal_pin_t pin)
{
    GPIO_TypeDef *port;

    if (!stm32_pin_valid(pin)) {
        return HAL_INVALID_PARAM;
    }
    port = stm32_gpio_port(pin);
    HAL_GPIO_DeInit(port, (uint32_t)(1UL << HAL_PIN_GET_INDEX(pin)));
    return HAL_OK;
}

int hal_gpio_write(hal_pin_t pin, hal_gpio_level_e level)
{
    if (!stm32_pin_valid(pin)) {
        return HAL_INVALID_PARAM;
    }
    HAL_GPIO_WritePin(stm32_gpio_port(pin), (uint16_t)(1UL << HAL_PIN_GET_INDEX(pin)),
                      (level == HAL_GPIO_HIGH) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    return HAL_OK;
}

int hal_gpio_read(hal_pin_t pin, hal_gpio_level_e *level)
{
    if (!stm32_pin_valid(pin) || level == NULL) {
        return HAL_INVALID_PARAM;
    }
    *level = (HAL_GPIO_ReadPin(stm32_gpio_port(pin),
                               (uint16_t)(1UL << HAL_PIN_GET_INDEX(pin))) == GPIO_PIN_SET)
             ? HAL_GPIO_HIGH : HAL_GPIO_LOW;
    return HAL_OK;
}

int hal_gpio_toggle(hal_pin_t pin)
{
    if (!stm32_pin_valid(pin)) {
        return HAL_INVALID_PARAM;
    }
    HAL_GPIO_TogglePin(stm32_gpio_port(pin), (uint16_t)(1UL << HAL_PIN_GET_INDEX(pin)));
    return HAL_OK;
}

int hal_gpio_set(hal_pin_t pin)
{
    return hal_gpio_write(pin, HAL_GPIO_HIGH);
}

int hal_gpio_reset(hal_pin_t pin)
{
    return hal_gpio_write(pin, HAL_GPIO_LOW);
}

/* ---------------- 引脚复用 ---------------- */
int hal_pin_init(const hal_pin_config_t *config)
{
    GPIO_InitTypeDef init;
    GPIO_TypeDef *port;

    if (config == NULL || !stm32_pin_valid(config->pin)) {
        return HAL_INVALID_PARAM;
    }
    if (config->function == HAL_PIN_FUNCTION_AF && config->alternate > 15U) {
        return HAL_INVALID_PARAM;
    }
    port = stm32_gpio_port(config->pin);
    stm32_gpio_clock_enable(HAL_PIN_GET_PORT(config->pin));

    memset(&init, 0, sizeof(init));
    init.Pin = (uint32_t)(1UL << HAL_PIN_GET_INDEX(config->pin));
    init.Pull = GPIO_NOPULL;
    init.Speed = GPIO_SPEED_FREQ_HIGH;
    init.Alternate = config->alternate;

    switch (config->function) {
    case HAL_PIN_FUNCTION_GPIO:
        init.Mode = GPIO_MODE_INPUT;
        break;
    case HAL_PIN_FUNCTION_AF:
        init.Mode = GPIO_MODE_AF_PP;
        break;
    case HAL_PIN_FUNCTION_ANALOG:
        init.Mode = GPIO_MODE_ANALOG;
        break;
    default:
        return HAL_INVALID_PARAM;
    }
    HAL_GPIO_Init(port, &init);
    return HAL_OK;
}

int hal_pin_deinit(hal_pin_t pin)
{
    return hal_gpio_deinit(pin);
}

/* ---------------- EXTI ---------------- */
static IRQn_Type stm32_exti_irq(uint8_t line)
{
    if (line < 5U) {
        return (IRQn_Type)(EXTI0_IRQn + line);
    }
    if (line < 10U) {
        return EXTI9_5_IRQn;
    }
    return EXTI15_10_IRQn;
}

int hal_exti_init(hal_pin_t pin, const hal_exti_config_t *config)
{
    GPIO_InitTypeDef init;
    GPIO_TypeDef *port;
    uint8_t line;
    uint32_t mode;

    if (config == NULL || !stm32_pin_valid(pin)) {
        return HAL_INVALID_PARAM;
    }
    switch (config->trigger) {
    case HAL_EXTI_RISING:  mode = GPIO_MODE_IT_RISING; break;
    case HAL_EXTI_FALLING: mode = GPIO_MODE_IT_FALLING; break;
    case HAL_EXTI_BOTH:    mode = GPIO_MODE_IT_RISING_FALLING; break;
    default: return HAL_INVALID_PARAM;
    }

    line = (uint8_t)HAL_PIN_GET_INDEX(pin);
    port = stm32_gpio_port(pin);
    stm32_gpio_clock_enable(HAL_PIN_GET_PORT(pin));

    memset(&init, 0, sizeof(init));
    init.Pin = (uint32_t)(1UL << line);
    init.Mode = mode;
    init.Pull = GPIO_NOPULL;
    init.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(port, &init);

    s_exti[line].callback = config->callback;
    s_exti[line].user_data = config->user_data;
    s_exti[line].pin = pin;
    s_exti[line].enabled = true;

    NVIC_SetPriority(stm32_exti_irq(line), (uint32_t)HAL_MIN(config->priority, 15U));
    NVIC_EnableIRQ(stm32_exti_irq(line));
    return HAL_OK;
}

int hal_exti_enable(hal_pin_t pin)
{
    uint8_t line;

    if (!stm32_pin_valid(pin)) {
        return HAL_INVALID_PARAM;
    }
    line = (uint8_t)HAL_PIN_GET_INDEX(pin);
    if (!s_exti[line].enabled) {
        return HAL_NOT_READY;
    }
    EXTI->IMR1 |= (uint32_t)(1UL << line);
    NVIC_EnableIRQ(stm32_exti_irq(line));
    return HAL_OK;
}

int hal_exti_disable(hal_pin_t pin)
{
    uint8_t line;

    if (!stm32_pin_valid(pin)) {
        return HAL_INVALID_PARAM;
    }
    line = (uint8_t)HAL_PIN_GET_INDEX(pin);
    EXTI->IMR1 &= ~(uint32_t)(1UL << line);
    NVIC_DisableIRQ(stm32_exti_irq(line));
    return HAL_OK;
}

int hal_exti_clear(hal_pin_t pin)
{
    if (!stm32_pin_valid(pin)) {
        return HAL_INVALID_PARAM;
    }
    __HAL_GPIO_EXTI_CLEAR_IT((uint16_t)(1UL << HAL_PIN_GET_INDEX(pin)));
    return HAL_OK;
}

int hal_exti_deinit(hal_pin_t pin)
{
    uint8_t line;

    if (!stm32_pin_valid(pin)) {
        return HAL_INVALID_PARAM;
    }
    line = (uint8_t)HAL_PIN_GET_INDEX(pin);
    HAL_GPIO_DeInit(stm32_gpio_port(pin), (uint32_t)(1UL << line));
    s_exti[line].callback = NULL;
    s_exti[line].user_data = NULL;
    s_exti[line].enabled = false;
    NVIC_DisableIRQ(stm32_exti_irq(line));
    return HAL_OK;
}

static void stm32_exti_dispatch(uint16_t gpio_pin)
{
    uint8_t line;

    for (line = 0U; line < STM32_LINE_MAX; line++) {
        if (((gpio_pin & (uint16_t)(1UL << line)) != 0U) &&
            s_exti[line].enabled && s_exti[line].callback != NULL) {
            s_exti[line].callback(s_exti[line].pin, s_exti[line].user_data);
        }
    }
}

/* ---------------- 槽位查找 ---------------- */
static stm32_slot_t *stm32_slot_find(stm32_slot_kind_t kind, const void *hw)
{
    uint8_t i;

    for (i = 0U; i < STM32_SLOT_MAX; i++) {
        if (s_slots[i].kind == kind && (const void *)&s_slots[i].hw == hw) {
            return &s_slots[i];
        }
    }
    return NULL;
}

/* ---------------- DMA ---------------- */
static uint32_t stm32_dma_alignment(hal_dma_width_e width)
{
    switch (width) {
    case HAL_DMA_WIDTH_16BIT: return DMA_PDATAALIGN_HALFWORD;
    case HAL_DMA_WIDTH_32BIT: return DMA_PDATAALIGN_WORD;
    default:                   return DMA_PDATAALIGN_BYTE;
    }
}

static uint32_t stm32_dma_mem_alignment(hal_dma_width_e width)
{
    switch (width) {
    case HAL_DMA_WIDTH_16BIT: return DMA_MDATAALIGN_HALFWORD;
    case HAL_DMA_WIDTH_32BIT: return DMA_MDATAALIGN_WORD;
    default:                   return DMA_MDATAALIGN_BYTE;
    }
}

static uint32_t stm32_dma_priority(hal_dma_priority_e priority)
{
    switch (priority) {
    case HAL_DMA_PRIORITY_MEDIUM:    return DMA_PRIORITY_MEDIUM;
    case HAL_DMA_PRIORITY_HIGH:      return DMA_PRIORITY_HIGH;
    case HAL_DMA_PRIORITY_VERY_HIGH: return DMA_PRIORITY_VERY_HIGH;
    default:                          return DMA_PRIORITY_LOW;
    }
}

static void stm32_dma_callback(DMA_HandleTypeDef *hdma, hal_event_e event)
{
    stm32_slot_t *slot = stm32_slot_find(STM32_SLOT_DMA, hdma);
    hal_dma_t *handle;

    if (slot == NULL || slot->owner == NULL) {
        return;
    }
    handle = (hal_dma_t *)slot->owner;
    if (event == HAL_EVENT_ERROR) {
        handle->base.error = HAL_IO_ERROR;
        handle->base.state = HAL_STATE_ERROR;
    } else {
        handle->base.state = HAL_STATE_READY;
    }
    if (handle->callback != NULL) {
        handle->callback(handle, event, handle->user_data);
    }
}

static void stm32_dma_xfer_cplt(DMA_HandleTypeDef *hdma)
{
    stm32_dma_callback(hdma, HAL_EVENT_COMPLETE);
}

static void stm32_dma_xfer_half(DMA_HandleTypeDef *hdma)
{
    stm32_dma_callback(hdma, HAL_EVENT_HALF_COMPLETE);
}

static void stm32_dma_xfer_error(DMA_HandleTypeDef *hdma)
{
    stm32_dma_callback(hdma, HAL_EVENT_ERROR);
}

int hal_dma_init(hal_dma_t *handle, const hal_dma_config_t *config)
{
    stm32_slot_t *slot;
    uint8_t instance;
    HAL_StatusTypeDef status;
    DMA_HandleTypeDef *hdma;

    if (handle == NULL || config == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state != HAL_STATE_RESET) {
        return HAL_ALREADY_INIT;
    }
    slot = stm32_slot_alloc(STM32_SLOT_DMA, (uint8_t)HAL_ARRAY_SIZE(s_dma_streams), &instance);
    if (slot == NULL) {
        return HAL_NO_MEMORY;
    }
    hdma = &slot->hw.dma;
    memset(hdma, 0, sizeof(*hdma));
    hdma->Instance = s_dma_streams[instance];
    hdma->Init.Request = config->request;
    hdma->Init.Direction = (config->direction == HAL_DMA_MEMORY_TO_MEMORY)
                           ? DMA_MEMORY_TO_MEMORY
                           : ((config->direction == HAL_DMA_MEMORY_TO_PERIPH)
                              ? DMA_MEMORY_TO_PERIPH : DMA_PERIPH_TO_MEMORY);
    hdma->Init.Priority = stm32_dma_priority(config->priority);
    hdma->Init.Mode = (config->mode == HAL_DMA_MODE_CIRCULAR)
                      ? DMA_CIRCULAR : DMA_NORMAL;
    hdma->Init.FIFOMode = DMA_FIFOMODE_DISABLE;
    hdma->Init.FIFOThreshold = DMA_FIFO_THRESHOLD_FULL;
    hdma->Init.MemBurst = DMA_MBURST_SINGLE;
    hdma->Init.PeriphBurst = DMA_PBURST_SINGLE;

    if (config->direction == HAL_DMA_MEMORY_TO_PERIPH) {
        hdma->Init.PeriphInc = config->dst_increment ? DMA_PINC_ENABLE : DMA_PINC_DISABLE;
        hdma->Init.MemInc = config->src_increment ? DMA_MINC_ENABLE : DMA_MINC_DISABLE;
        hdma->Init.PeriphDataAlignment = stm32_dma_alignment(config->dst_width);
        hdma->Init.MemDataAlignment = stm32_dma_mem_alignment(config->src_width);
    } else if (config->direction == HAL_DMA_PERIPH_TO_MEMORY) {
        hdma->Init.PeriphInc = config->src_increment ? DMA_PINC_ENABLE : DMA_PINC_DISABLE;
        hdma->Init.MemInc = config->dst_increment ? DMA_MINC_ENABLE : DMA_MINC_DISABLE;
        hdma->Init.PeriphDataAlignment = stm32_dma_alignment(config->src_width);
        hdma->Init.MemDataAlignment = stm32_dma_mem_alignment(config->dst_width);
    } else {
        hdma->Init.PeriphInc = config->src_increment ? DMA_PINC_ENABLE : DMA_PINC_DISABLE;
        hdma->Init.MemInc = config->dst_increment ? DMA_MINC_ENABLE : DMA_MINC_DISABLE;
        hdma->Init.PeriphDataAlignment = stm32_dma_alignment(config->src_width);
        hdma->Init.MemDataAlignment = stm32_dma_mem_alignment(config->dst_width);
    }

    if (instance < 8U) {
        __HAL_RCC_DMA1_CLK_ENABLE();
    } else {
        __HAL_RCC_DMA2_CLK_ENABLE();
    }

    status = HAL_DMA_Init(hdma);
    if (status != HAL_OK) {
        stm32_slot_free(slot);
        return stm32_from_hal(status);
    }
    hdma->XferCpltCallback = stm32_dma_xfer_cplt;
    hdma->XferHalfCpltCallback = stm32_dma_xfer_half;
    hdma->XferErrorCallback = stm32_dma_xfer_error;

    slot->owner = handle;
    handle->base.instance = slot;
    handle->config = *config;
    handle->callback = NULL;
    handle->user_data = NULL;
    return stm32_base_init(&handle->base, slot);
}

int hal_dma_deinit(hal_dma_t *handle)
{
    stm32_slot_t *slot;

    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    slot = (stm32_slot_t *)handle->base.instance;
    if (slot == NULL) {
        return HAL_NOT_READY;
    }
    (void)HAL_DMA_Abort(&slot->hw.dma);
    (void)HAL_DMA_DeInit(&slot->hw.dma);
    stm32_slot_free(slot);
    stm32_base_reset(&handle->base);
    return HAL_OK;
}

int hal_dma_start(hal_dma_t *handle, uintptr_t src,
                             uintptr_t dst, size_t length)
{
    stm32_slot_t *slot;
    HAL_StatusTypeDef status;

    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    if (length == 0U) {
        return HAL_INVALID_PARAM;
    }
    slot = (stm32_slot_t *)handle->base.instance;
    status = HAL_DMA_Start_IT(&slot->hw.dma, (uint32_t)src, (uint32_t)dst,
                              (uint32_t)length);
    if (status != HAL_OK) {
        return stm32_from_hal(status);
    }
    handle->base.state = HAL_STATE_BUSY;
    return HAL_OK;
}

int hal_dma_stop(hal_dma_t *handle)
{
    stm32_slot_t *slot;
    HAL_StatusTypeDef status;

    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    slot = (stm32_slot_t *)handle->base.instance;
    status = HAL_DMA_Abort(&slot->hw.dma);
    handle->base.state = HAL_STATE_READY;
    return (status == HAL_OK || status == HAL_ERROR) ? HAL_OK : stm32_from_hal(status);
}

int hal_dma_suspend(hal_dma_t *handle)
{
    stm32_slot_t *slot;

    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    slot = (stm32_slot_t *)handle->base.instance;
    __HAL_DMA_DISABLE(&slot->hw.dma);
    handle->base.state = HAL_STATE_SUSPENDED;
    return HAL_OK;
}

int hal_dma_resume(hal_dma_t *handle)
{
    stm32_slot_t *slot;

    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    slot = (stm32_slot_t *)handle->base.instance;
    __HAL_DMA_ENABLE(&slot->hw.dma);
    handle->base.state = HAL_STATE_BUSY;
    return HAL_OK;
}

size_t hal_dma_get_remaining(hal_dma_t *handle)
{
    stm32_slot_t *slot;

    if (handle == NULL || handle->base.state == HAL_STATE_RESET) {
        return 0U;
    }
    slot = (stm32_slot_t *)handle->base.instance;
    return (slot != NULL) ? (size_t)__HAL_DMA_GET_COUNTER(&slot->hw.dma) : 0U;
}

/* ---------------- UART ---------------- */
static uint32_t stm32_uart_word(hal_uart_word_e word)
{
    switch (word) {
    case HAL_UART_WORD_7BIT: return UART_WORDLENGTH_7B;
    case HAL_UART_WORD_9BIT: return UART_WORDLENGTH_9B;
    default:                  return UART_WORDLENGTH_8B;
    }
}

static uint32_t stm32_uart_parity(hal_uart_parity_e parity)
{
    switch (parity) {
    case HAL_UART_PARITY_EVEN: return UART_PARITY_EVEN;
    case HAL_UART_PARITY_ODD:  return UART_PARITY_ODD;
    default:                    return UART_PARITY_NONE;
    }
}

static uint32_t stm32_uart_flow(hal_uart_flow_e flow)
{
    switch (flow) {
    case HAL_UART_FLOW_RTS:     return UART_HWCONTROL_RTS;
    case HAL_UART_FLOW_CTS:     return UART_HWCONTROL_CTS;
    case HAL_UART_FLOW_RTS_CTS: return UART_HWCONTROL_RTS_CTS;
    default:                     return UART_HWCONTROL_NONE;
    }
}

static uint32_t stm32_uart_mode(hal_uart_mode_e mode)
{
    switch (mode) {
    case HAL_UART_MODE_TX: return UART_MODE_TX;
    case HAL_UART_MODE_RX: return UART_MODE_RX;
    default:                return UART_MODE_TX_RX;
    }
}

int hal_uart_init(hal_uart_t *handle, const hal_uart_config_t *config)
{
    stm32_slot_t *slot;
    uint8_t instance;
    HAL_StatusTypeDef status;
    UART_HandleTypeDef *huart;

    if (handle == NULL || config == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (config->baudrate == 0U) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state != HAL_STATE_RESET) {
        return HAL_ALREADY_INIT;
    }
    slot = stm32_slot_alloc(STM32_SLOT_UART, (uint8_t)HAL_ARRAY_SIZE(s_uart_bases), &instance);
    if (slot == NULL) {
        return HAL_NO_MEMORY;
    }
    huart = &slot->hw.uart;
    memset(huart, 0, sizeof(*huart));
    huart->Instance = s_uart_bases[instance];
    huart->Init.BaudRate = config->baudrate;
    huart->Init.WordLength = stm32_uart_word(config->word_length);
    huart->Init.StopBits = (config->stop_bits == HAL_UART_STOP_2)
                           ? UART_STOPBITS_2 : UART_STOPBITS_1;
    huart->Init.Parity = stm32_uart_parity(config->parity);
    huart->Init.Mode = stm32_uart_mode(config->mode);
    huart->Init.HwFlowCtl = stm32_uart_flow(config->flow_control);
    huart->Init.OverSampling = config->oversampling ? UART_OVERSAMPLING_16
                                                    : UART_OVERSAMPLING_8;
    huart->Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
    huart->Init.ClockPrescaler = UART_PRESCALER_DIV1;
    huart->AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
    huart->FIFOMode = UART_FIFOMODE_DISABLE;
    huart->TXFIFOThreshold = UART_TXFIFO_THRESHOLD_1_8;
    huart->RXFIFOThreshold = UART_RXFIFO_THRESHOLD_1_8;

    status = HAL_UART_Init(huart);
    if (status != HAL_OK) {
        stm32_slot_free(slot);
        return stm32_from_hal(status);
    }
    slot->owner = handle;
    slot->rx_length = 0U;
    handle->config = *config;
    handle->callback = NULL;
    handle->user_data = NULL;
    handle->tx_dma = NULL;
    handle->rx_dma = NULL;
    return stm32_base_init(&handle->base, slot);
}

int hal_uart_deinit(hal_uart_t *handle)
{
    stm32_slot_t *slot;

    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    slot = (stm32_slot_t *)handle->base.instance;
    (void)HAL_UART_DeInit(&slot->hw.uart);
    stm32_slot_free(slot);
    stm32_base_reset(&handle->base);
    return HAL_OK;
}

int hal_uart_transmit(hal_uart_t *handle, const uint8_t *data,
                                 size_t length, uint32_t timeout)
{
    stm32_slot_t *slot;
    HAL_StatusTypeDef status;

    if (handle == NULL || data == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    if (length == 0U || length > 0xFFFFU) {
        return HAL_INVALID_PARAM;
    }
    slot = (stm32_slot_t *)handle->base.instance;
    handle->base.state = HAL_STATE_BUSY_TX;
    status = HAL_UART_Transmit(&slot->hw.uart, (uint8_t *)data, (uint16_t)length,
                               stm32_timeout(timeout));
    handle->base.state = (status == HAL_OK) ? HAL_STATE_READY : HAL_STATE_ERROR;
    if (status != HAL_OK) {
        handle->base.error = stm32_from_hal(status);
    }
    return stm32_from_hal(status);
}

int hal_uart_receive(hal_uart_t *handle, uint8_t *data,
                                size_t length, uint32_t timeout)
{
    stm32_slot_t *slot;
    HAL_StatusTypeDef status;

    if (handle == NULL || data == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    if (length == 0U || length > 0xFFFFU) {
        return HAL_INVALID_PARAM;
    }
    slot = (stm32_slot_t *)handle->base.instance;
    handle->base.state = HAL_STATE_BUSY_RX;
    status = HAL_UART_Receive(&slot->hw.uart, data, (uint16_t)length,
                              stm32_timeout(timeout));
    handle->base.state = (status == HAL_OK) ? HAL_STATE_READY : HAL_STATE_ERROR;
    if (status != HAL_OK) {
        handle->base.error = stm32_from_hal(status);
    }
    return stm32_from_hal(status);
}

int hal_uart_transmit_it(hal_uart_t *handle, const uint8_t *data,
                                    size_t length)
{
    stm32_slot_t *slot;
    HAL_StatusTypeDef status;

    if (handle == NULL || data == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    if (length == 0U || length > 0xFFFFU) {
        return HAL_INVALID_PARAM;
    }
    slot = (stm32_slot_t *)handle->base.instance;
    status = HAL_UART_Transmit_IT(&slot->hw.uart, (uint8_t *)data, (uint16_t)length);
    if (status == HAL_OK) {
        handle->base.state = HAL_STATE_BUSY_TX;
    }
    return stm32_from_hal(status);
}

int hal_uart_receive_it(hal_uart_t *handle, uint8_t *data,
                                   size_t length)
{
    stm32_slot_t *slot;
    HAL_StatusTypeDef status;

    if (handle == NULL || data == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    if (length == 0U || length > 0xFFFFU) {
        return HAL_INVALID_PARAM;
    }
    slot = (stm32_slot_t *)handle->base.instance;
    status = HAL_UART_Receive_IT(&slot->hw.uart, data, (uint16_t)length);
    if (status == HAL_OK) {
        handle->base.state = HAL_STATE_BUSY_RX;
    }
    return stm32_from_hal(status);
}

int hal_uart_transmit_dma(hal_uart_t *handle, const uint8_t *data,
                                     size_t length)
{
    stm32_slot_t *slot;
    HAL_StatusTypeDef status;

    if (handle == NULL || data == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    if (length == 0U || length > 0xFFFFU) {
        return HAL_INVALID_PARAM;
    }
    slot = (stm32_slot_t *)handle->base.instance;
    status = HAL_UART_Transmit_DMA(&slot->hw.uart, (uint8_t *)data, (uint16_t)length);
    if (status == HAL_OK) {
        handle->base.state = HAL_STATE_BUSY_TX;
    }
    return stm32_from_hal(status);
}

int hal_uart_receive_dma(hal_uart_t *handle, uint8_t *data,
                                    size_t length)
{
    stm32_slot_t *slot;
    HAL_StatusTypeDef status;

    if (handle == NULL || data == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    if (length == 0U || length > 0xFFFFU) {
        return HAL_INVALID_PARAM;
    }
    slot = (stm32_slot_t *)handle->base.instance;
    status = HAL_UART_Receive_DMA(&slot->hw.uart, data, (uint16_t)length);
    if (status == HAL_OK) {
        handle->base.state = HAL_STATE_BUSY_RX;
    }
    return stm32_from_hal(status);
}

int hal_uart_receive_to_idle(hal_uart_t *handle, uint8_t *data,
                                        size_t length, hal_transfer_mode_e mode)
{
    stm32_slot_t *slot;
    HAL_StatusTypeDef status;

    if (handle == NULL || data == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    if (length == 0U || length > 0xFFFFU) {
        return HAL_INVALID_PARAM;
    }
    slot = (stm32_slot_t *)handle->base.instance;
    if (mode == HAL_MODE_IT) {
        status = HAL_UARTEx_ReceiveToIdle_IT(&slot->hw.uart, data, (uint16_t)length);
    } else if (mode == HAL_MODE_DMA) {
        status = HAL_UARTEx_ReceiveToIdle_DMA(&slot->hw.uart, data, (uint16_t)length);
    } else {
        return HAL_NOT_SUPPORTED;
    }
    if (status == HAL_OK) {
        handle->base.state = HAL_STATE_BUSY_RX;
    }
    return stm32_from_hal(status);
}

size_t hal_uart_get_rx_count(hal_uart_t *handle)
{
    stm32_slot_t *slot;

    if (handle == NULL || handle->base.state == HAL_STATE_RESET) {
        return 0U;
    }
    slot = (stm32_slot_t *)handle->base.instance;
    return (slot != NULL) ? (size_t)slot->rx_length : 0U;
}

int hal_uart_abort_tx(hal_uart_t *handle)
{
    stm32_slot_t *slot;

    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    slot = (stm32_slot_t *)handle->base.instance;
    (void)HAL_UART_AbortTransmit(&slot->hw.uart);
    handle->base.state = HAL_STATE_READY;
    return HAL_OK;
}

int hal_uart_abort_rx(hal_uart_t *handle)
{
    stm32_slot_t *slot;

    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    slot = (stm32_slot_t *)handle->base.instance;
    (void)HAL_UART_AbortReceive(&slot->hw.uart);
    handle->base.state = HAL_STATE_READY;
    return HAL_OK;
}

int hal_uart_abort(hal_uart_t *handle)
{
    stm32_slot_t *slot;

    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    slot = (stm32_slot_t *)handle->base.instance;
    (void)HAL_UART_Abort(&slot->hw.uart);
    handle->base.state = HAL_STATE_READY;
    return HAL_OK;
}

hal_state_e hal_uart_get_state(hal_uart_t *handle)
{
    return (handle != NULL) ? handle->base.state : HAL_STATE_RESET;
}

int hal_uart_get_error(hal_uart_t *handle)
{
    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    return handle->base.error;
}

int hal_uart_register_callback(hal_uart_t *handle,
                                          hal_uart_callback_t callback, void *user_data)
{
    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    handle->callback = callback;
    handle->user_data = user_data;
    return HAL_OK;
}

/* ---------------- SPI ---------------- */
static uint32_t stm32_spi_clock(uint8_t instance)
{
    switch (instance) {
    case 0U:                        /* SPI1 */
    case 3U:                        /* SPI4 */
    case 4U:                        /* SPI5 */
        return (uint32_t)HAL_RCC_GetPCLK2Freq();
    case 5U:                        /* SPI6 */
        return (uint32_t)HAL_RCC_GetPCLK4Freq();
    default:                        /* SPI2 / SPI3 */
        return (uint32_t)HAL_RCC_GetPCLK1Freq();
    }
}

static uint32_t stm32_spi_prescaler(uint32_t pclk, uint32_t baudrate)
{
    static const uint32_t divisors[] = { 2U, 4U, 8U, 16U, 32U, 64U, 128U, 256U };
    static const uint32_t values[] = {
        SPI_BAUDRATEPRESCALER_2, SPI_BAUDRATEPRESCALER_4,
        SPI_BAUDRATEPRESCALER_8, SPI_BAUDRATEPRESCALER_16,
        SPI_BAUDRATEPRESCALER_32, SPI_BAUDRATEPRESCALER_64,
        SPI_BAUDRATEPRESCALER_128, SPI_BAUDRATEPRESCALER_256
    };
    uint32_t need;
    uint8_t i;

    if (baudrate == 0U) {
        return SPI_BAUDRATEPRESCALER_2;
    }
    need = (pclk + baudrate - 1U) / baudrate;
    for (i = 0U; i < HAL_ARRAY_SIZE(divisors); i++) {
        if (divisors[i] >= need) {
            return values[i];
        }
    }
    return SPI_BAUDRATEPRESCALER_256;
}

static uint32_t stm32_spi_direction(hal_spi_direction_e direction)
{
    switch (direction) {
    case HAL_SPI_DIRECTION_2LINES_RXONLY: return SPI_DIRECTION_2LINES_RXONLY;
    case HAL_SPI_DIRECTION_1LINE:         return SPI_DIRECTION_1LINE;
    default:                               return SPI_DIRECTION_2LINES;
    }
}

static size_t stm32_spi_frames(const hal_spi_t *handle, size_t length)
{
    return (handle->config.data_bits > 8U) ? (length / 2U) : length;
}

int hal_spi_init(hal_spi_t *handle, const hal_spi_config_t *config)
{
    stm32_slot_t *slot;
    uint8_t instance;
    HAL_StatusTypeDef status;
    SPI_HandleTypeDef *hspi;

    if (handle == NULL || config == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (config->data_bits != 8U && config->data_bits != 16U) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state != HAL_STATE_RESET) {
        return HAL_ALREADY_INIT;
    }
    slot = stm32_slot_alloc(STM32_SLOT_SPI, (uint8_t)HAL_ARRAY_SIZE(s_spi_bases), &instance);
    if (slot == NULL) {
        return HAL_NO_MEMORY;
    }
    hspi = &slot->hw.spi;
    memset(hspi, 0, sizeof(*hspi));
    hspi->Instance = s_spi_bases[instance];
    hspi->Init.Mode = (config->mode == HAL_SPI_MODE_SLAVE) ? SPI_MODE_SLAVE
                                                            : SPI_MODE_MASTER;
    hspi->Init.Direction = stm32_spi_direction(config->direction);
    hspi->Init.DataSize = (config->data_bits == 16U) ? SPI_DATASIZE_16BIT
                                                     : SPI_DATASIZE_8BIT;
    hspi->Init.CLKPolarity = (config->cpol == HAL_SPI_CPOL_HIGH) ? SPI_POLARITY_HIGH
                                                                  : SPI_POLARITY_LOW;
    hspi->Init.CLKPhase = (config->cpha == HAL_SPI_CPHA_2EDGE) ? SPI_PHASE_2EDGE
                                                                : SPI_PHASE_1EDGE;
    hspi->Init.NSS = SPI_NSS_SOFT;
    hspi->Init.BaudRatePrescaler = (config->mode == HAL_SPI_MODE_MASTER)
                                   ? stm32_spi_prescaler(stm32_spi_clock(instance), config->baudrate)
                                   : SPI_BAUDRATEPRESCALER_2;
    hspi->Init.FirstBit = (config->bit_order == HAL_SPI_BITORDER_LSB)
                          ? SPI_FIRSTBIT_LSB : SPI_FIRSTBIT_MSB;
    hspi->Init.TIMode = SPI_TIMODE_DISABLE;
    hspi->Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    hspi->Init.CRCPolynomial = 7U;
    hspi->Init.CRCLength = SPI_CRC_LENGTH_DATASIZE;
    hspi->Init.NSSPMode = SPI_NSS_PULSE_DISABLE;

    status = HAL_SPI_Init(hspi);
    if (status != HAL_OK) {
        stm32_slot_free(slot);
        return stm32_from_hal(status);
    }
    slot->owner = handle;
    handle->config = *config;
    handle->callback = NULL;
    handle->user_data = NULL;
    handle->tx_dma = NULL;
    handle->rx_dma = NULL;
    return stm32_base_init(&handle->base, slot);
}

int hal_spi_deinit(hal_spi_t *handle)
{
    stm32_slot_t *slot;

    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    slot = (stm32_slot_t *)handle->base.instance;
    (void)HAL_SPI_DeInit(&slot->hw.spi);
    stm32_slot_free(slot);
    stm32_base_reset(&handle->base);
    return HAL_OK;
}

int hal_spi_transmit(hal_spi_t *handle, const void *data,
                                size_t length, uint32_t timeout)
{
    stm32_slot_t *slot;
    size_t frames;
    HAL_StatusTypeDef status;

    if (handle == NULL || data == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    frames = stm32_spi_frames(handle, length);
    if (frames == 0U || frames > 0xFFFFU) {
        return HAL_INVALID_PARAM;
    }
    slot = (stm32_slot_t *)handle->base.instance;
    handle->base.state = HAL_STATE_BUSY_TX;
    status = HAL_SPI_Transmit(&slot->hw.spi, (uint8_t *)data, (uint16_t)frames,
                              stm32_timeout(timeout));
    handle->base.state = (status == HAL_OK) ? HAL_STATE_READY : HAL_STATE_ERROR;
    return stm32_from_hal(status);
}

int hal_spi_receive(hal_spi_t *handle, void *data,
                               size_t length, uint32_t timeout)
{
    stm32_slot_t *slot;
    size_t frames;
    HAL_StatusTypeDef status;

    if (handle == NULL || data == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    frames = stm32_spi_frames(handle, length);
    if (frames == 0U || frames > 0xFFFFU) {
        return HAL_INVALID_PARAM;
    }
    slot = (stm32_slot_t *)handle->base.instance;
    handle->base.state = HAL_STATE_BUSY_RX;
    status = HAL_SPI_Receive(&slot->hw.spi, data, (uint16_t)frames,
                             stm32_timeout(timeout));
    handle->base.state = (status == HAL_OK) ? HAL_STATE_READY : HAL_STATE_ERROR;
    return stm32_from_hal(status);
}

int hal_spi_transfer(hal_spi_t *handle, const void *tx_data,
                                void *rx_data, size_t length, uint32_t timeout)
{
    stm32_slot_t *slot;
    size_t frames;
    HAL_StatusTypeDef status;

    if (handle == NULL || tx_data == NULL || rx_data == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    frames = stm32_spi_frames(handle, length);
    if (frames == 0U || frames > 0xFFFFU) {
        return HAL_INVALID_PARAM;
    }
    slot = (stm32_slot_t *)handle->base.instance;
    handle->base.state = HAL_STATE_BUSY_TX_RX;
    status = HAL_SPI_TransmitReceive(&slot->hw.spi, (uint8_t *)tx_data,
                                     (uint8_t *)rx_data, (uint16_t)frames,
                                     stm32_timeout(timeout));
    handle->base.state = (status == HAL_OK) ? HAL_STATE_READY : HAL_STATE_ERROR;
    return stm32_from_hal(status);
}

int hal_spi_transmit_dma(hal_spi_t *handle, const void *data,
                                    size_t length)
{
    stm32_slot_t *slot;
    size_t frames;
    HAL_StatusTypeDef status;

    if (handle == NULL || data == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    frames = stm32_spi_frames(handle, length);
    if (frames == 0U || frames > 0xFFFFU) {
        return HAL_INVALID_PARAM;
    }
    slot = (stm32_slot_t *)handle->base.instance;
    status = HAL_SPI_Transmit_DMA(&slot->hw.spi, (uint8_t *)data, (uint16_t)frames);
    if (status == HAL_OK) {
        handle->base.state = HAL_STATE_BUSY_TX;
    }
    return stm32_from_hal(status);
}

int hal_spi_receive_dma(hal_spi_t *handle, void *data,
                                   size_t length)
{
    stm32_slot_t *slot;
    size_t frames;
    HAL_StatusTypeDef status;

    if (handle == NULL || data == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    frames = stm32_spi_frames(handle, length);
    if (frames == 0U || frames > 0xFFFFU) {
        return HAL_INVALID_PARAM;
    }
    slot = (stm32_slot_t *)handle->base.instance;
    status = HAL_SPI_Receive_DMA(&slot->hw.spi, data, (uint16_t)frames);
    if (status == HAL_OK) {
        handle->base.state = HAL_STATE_BUSY_RX;
    }
    return stm32_from_hal(status);
}

int hal_spi_transfer_dma(hal_spi_t *handle, const void *tx_data,
                                    void *rx_data, size_t length)
{
    stm32_slot_t *slot;
    size_t frames;
    HAL_StatusTypeDef status;

    if (handle == NULL || tx_data == NULL || rx_data == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    frames = stm32_spi_frames(handle, length);
    if (frames == 0U || frames > 0xFFFFU) {
        return HAL_INVALID_PARAM;
    }
    slot = (stm32_slot_t *)handle->base.instance;
    status = HAL_SPI_TransmitReceive_DMA(&slot->hw.spi, (uint8_t *)tx_data,
                                         (uint8_t *)rx_data, (uint16_t)frames);
    if (status == HAL_OK) {
        handle->base.state = HAL_STATE_BUSY_TX_RX;
    }
    return stm32_from_hal(status);
}

int hal_spi_set_speed(hal_spi_t *handle, uint32_t baudrate)
{
    stm32_slot_t *slot;
    HAL_StatusTypeDef status;

    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    if (baudrate == 0U) {
        return HAL_INVALID_PARAM;
    }
    slot = (stm32_slot_t *)handle->base.instance;
    slot->hw.spi.Init.BaudRatePrescaler =
        stm32_spi_prescaler(stm32_spi_clock(slot->index), baudrate);
    handle->config.baudrate = baudrate;
    status = HAL_SPI_Init(&slot->hw.spi);
    return stm32_from_hal(status);
}

int hal_spi_abort(hal_spi_t *handle)
{
    stm32_slot_t *slot;

    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    slot = (stm32_slot_t *)handle->base.instance;
    (void)HAL_SPI_Abort(&slot->hw.spi);
    handle->base.state = HAL_STATE_READY;
    return HAL_OK;
}

hal_state_e hal_spi_get_state(hal_spi_t *handle)
{
    return (handle != NULL) ? handle->base.state : HAL_STATE_RESET;
}

/* ---------------- I2C ---------------- */
static uint32_t stm32_i2c_timing(uint32_t pclk, hal_i2c_speed_e speed)
{
    uint32_t target = 100000U;
    uint32_t prescaler = 0U;
    uint32_t tick;
    uint32_t period;
    uint32_t scll;
    uint32_t sclh;

    if (speed == HAL_I2C_SPEED_FAST) {
        target = 400000U;
    } else if (speed == HAL_I2C_SPEED_FAST_PLUS) {
        target = 1000000U;
    }
    while (prescaler < 15U && (pclk / (prescaler + 1U)) > 4000000U) {
        prescaler++;
    }
    tick = pclk / (prescaler + 1U);
    period = tick / target;
    if (period < 4U) {
        period = 4U;
    }
    if (period > 1024U) {
        period = 1024U;
    }
    scll = period / 2U;
    sclh = period - scll - 2U;
    if (sclh < 1U) {
        sclh = 1U;
    }
    /* PRESC[3:0] | SCLDEL[3:0] | SDADEL[3:0] | SCLH[7:0] | SCLL[7:0] */
    return (prescaler << 28) | (4UL << 20) | (2UL << 16) | (sclh << 8) | scll;
}

static uint16_t stm32_i2c_address(const hal_i2c_t *handle, uint16_t address)
{
    if (handle->config.address_mode == HAL_I2C_ADDR_7BIT) {
        return (uint16_t)(address << 1);
    }
    return address;
}

int hal_i2c_init(hal_i2c_t *handle, const hal_i2c_config_t *config)
{
    stm32_slot_t *slot;
    uint8_t instance;
    HAL_StatusTypeDef status;
    I2C_HandleTypeDef *hi2c;
    uint32_t pclk;

    if (handle == NULL || config == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state != HAL_STATE_RESET) {
        return HAL_ALREADY_INIT;
    }
    slot = stm32_slot_alloc(STM32_SLOT_I2C, (uint8_t)HAL_ARRAY_SIZE(s_i2c_bases), &instance);
    if (slot == NULL) {
        return HAL_NO_MEMORY;
    }
    hi2c = &slot->hw.i2c;
    memset(hi2c, 0, sizeof(*hi2c));
    hi2c->Instance = s_i2c_bases[instance];
    pclk = (instance == 3U) ? (uint32_t)HAL_RCC_GetPCLK4Freq()
                            : (uint32_t)HAL_RCC_GetPCLK1Freq();
    hi2c->Init.Timing = stm32_i2c_timing(pclk, config->speed);
    hi2c->Init.OwnAddress1 = config->own_address;
    hi2c->Init.AddressingMode = (config->address_mode == HAL_I2C_ADDR_10BIT)
                                ? I2C_ADDRESSINGMODE_10BIT : I2C_ADDRESSINGMODE_7BIT;
    hi2c->Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c->Init.OwnAddress2 = 0U;
    hi2c->Init.OwnAddress2Masks = I2C_OA2_NOMASK;
    hi2c->Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c->Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;

    status = HAL_I2C_Init(hi2c);
    if (status != HAL_OK) {
        stm32_slot_free(slot);
        return stm32_from_hal(status);
    }
    slot->owner = handle;
    handle->config = *config;
    handle->callback = NULL;
    handle->user_data = NULL;
    handle->tx_dma = NULL;
    handle->rx_dma = NULL;
    return stm32_base_init(&handle->base, slot);
}

int hal_i2c_deinit(hal_i2c_t *handle)
{
    stm32_slot_t *slot;

    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    slot = (stm32_slot_t *)handle->base.instance;
    (void)HAL_I2C_DeInit(&slot->hw.i2c);
    stm32_slot_free(slot);
    stm32_base_reset(&handle->base);
    return HAL_OK;
}

int hal_i2c_master_transmit(hal_i2c_t *handle, uint16_t address,
                                       const uint8_t *data, size_t length,
                                       uint32_t timeout)
{
    stm32_slot_t *slot;
    HAL_StatusTypeDef status;

    if (handle == NULL || data == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    if (length == 0U || length > 0xFFFFU) {
        return HAL_INVALID_PARAM;
    }
    slot = (stm32_slot_t *)handle->base.instance;
    status = HAL_I2C_Master_Transmit(&slot->hw.i2c,
                                     stm32_i2c_address(handle, address),
                                     (uint8_t *)data, (uint16_t)length,
                                     stm32_timeout(timeout));
    if (status != HAL_OK) {
        handle->base.error = stm32_from_hal(status);
        return stm32_from_hal(status);
    }
    return HAL_OK;
}

int hal_i2c_master_receive(hal_i2c_t *handle, uint16_t address,
                                      uint8_t *data, size_t length, uint32_t timeout)
{
    stm32_slot_t *slot;
    HAL_StatusTypeDef status;

    if (handle == NULL || data == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    if (length == 0U || length > 0xFFFFU) {
        return HAL_INVALID_PARAM;
    }
    slot = (stm32_slot_t *)handle->base.instance;
    status = HAL_I2C_Master_Receive(&slot->hw.i2c,
                                    stm32_i2c_address(handle, address),
                                    data, (uint16_t)length,
                                    stm32_timeout(timeout));
    if (status != HAL_OK) {
        handle->base.error = stm32_from_hal(status);
        return stm32_from_hal(status);
    }
    return HAL_OK;
}

int hal_i2c_master_transfer(hal_i2c_t *handle, uint16_t address,
                                       const uint8_t *tx_data, size_t tx_length,
                                       uint8_t *rx_data, size_t rx_length,
                                       uint32_t timeout)
{
    int status;

    if (handle == NULL || (tx_data == NULL && rx_data == NULL)) {
        return HAL_INVALID_PARAM;
    }
    if (tx_length == 0U && rx_length == 0U) {
        return HAL_INVALID_PARAM;
    }
    /* 阻塞模式 HAL 不提供原子 Repeated Start，按“写后读”顺序执行；
     * 寄存器访问请使用 iot_i2c_mem_* 以获得真正的 Repeated Start。 */
    if (tx_length > 0U) {
        status = hal_i2c_master_transmit(handle, address, tx_data, tx_length, timeout);
        if (status != HAL_OK) {
            return status;
        }
    }
    if (rx_length > 0U) {
        return hal_i2c_master_receive(handle, address, rx_data, rx_length, timeout);
    }
    return HAL_OK;
}

int hal_i2c_mem_write(hal_i2c_t *handle, uint16_t address,
                                 uint16_t reg, uint8_t reg_size,
                                 const uint8_t *data, size_t length, uint32_t timeout)
{
    stm32_slot_t *slot;
    HAL_StatusTypeDef status;
    uint16_t mem_size;

    if (handle == NULL || data == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    if (length == 0U || length > 0xFFFFU || (reg_size != 8U && reg_size != 16U)) {
        return HAL_INVALID_PARAM;
    }
    mem_size = (reg_size == 16U) ? I2C_MEMADD_SIZE_16BIT : I2C_MEMADD_SIZE_8BIT;
    slot = (stm32_slot_t *)handle->base.instance;
    status = HAL_I2C_Mem_Write(&slot->hw.i2c, stm32_i2c_address(handle, address),
                               reg, mem_size, (uint8_t *)data, (uint16_t)length,
                               stm32_timeout(timeout));
    return stm32_from_hal(status);
}

int hal_i2c_mem_read(hal_i2c_t *handle, uint16_t address,
                                uint16_t reg, uint8_t reg_size,
                                uint8_t *data, size_t length, uint32_t timeout)
{
    stm32_slot_t *slot;
    HAL_StatusTypeDef status;
    uint16_t mem_size;

    if (handle == NULL || data == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    if (length == 0U || length > 0xFFFFU || (reg_size != 8U && reg_size != 16U)) {
        return HAL_INVALID_PARAM;
    }
    mem_size = (reg_size == 16U) ? I2C_MEMADD_SIZE_16BIT : I2C_MEMADD_SIZE_8BIT;
    slot = (stm32_slot_t *)handle->base.instance;
    status = HAL_I2C_Mem_Read(&slot->hw.i2c, stm32_i2c_address(handle, address),
                              reg, mem_size, data, (uint16_t)length,
                              stm32_timeout(timeout));
    return stm32_from_hal(status);
}

int hal_i2c_master_transmit_dma(hal_i2c_t *handle, uint16_t address,
                                           const uint8_t *data, size_t length)
{
    stm32_slot_t *slot;
    HAL_StatusTypeDef status;

    if (handle == NULL || data == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    if (length == 0U || length > 0xFFFFU) {
        return HAL_INVALID_PARAM;
    }
    slot = (stm32_slot_t *)handle->base.instance;
    status = HAL_I2C_Master_Transmit_DMA(&slot->hw.i2c,
                                         stm32_i2c_address(handle, address),
                                         (uint8_t *)data, (uint16_t)length);
    return stm32_from_hal(status);
}

int hal_i2c_master_receive_dma(hal_i2c_t *handle, uint16_t address,
                                          uint8_t *data, size_t length)
{
    stm32_slot_t *slot;
    HAL_StatusTypeDef status;

    if (handle == NULL || data == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    if (length == 0U || length > 0xFFFFU) {
        return HAL_INVALID_PARAM;
    }
    slot = (stm32_slot_t *)handle->base.instance;
    status = HAL_I2C_Master_Receive_DMA(&slot->hw.i2c,
                                        stm32_i2c_address(handle, address),
                                        data, (uint16_t)length);
    return stm32_from_hal(status);
}

int hal_i2c_is_device_ready(hal_i2c_t *handle, uint16_t address,
                                       uint32_t trials, uint32_t timeout)
{
    stm32_slot_t *slot;
    HAL_StatusTypeDef status;

    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    slot = (stm32_slot_t *)handle->base.instance;
    status = HAL_I2C_IsDeviceReady(&slot->hw.i2c, stm32_i2c_address(handle, address),
                                   trials, stm32_timeout(timeout));
    return stm32_from_hal(status);
}

int hal_i2c_bus_recover(hal_i2c_t *handle)
{
    stm32_slot_t *slot;

    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    slot = (stm32_slot_t *)handle->base.instance;
    (void)HAL_I2C_DeInit(&slot->hw.i2c);
    return stm32_from_hal(HAL_I2C_Init(&slot->hw.i2c));
}

int hal_i2c_abort(hal_i2c_t *handle)
{
    return hal_i2c_bus_recover(handle);
}

hal_state_e hal_i2c_get_state(hal_i2c_t *handle)
{
    return (handle != NULL) ? handle->base.state : HAL_STATE_RESET;
}

/* ---------------- I2S ---------------- */
static uint32_t stm32_i2s_mode(hal_i2s_mode_e mode, bool *tx)
{
    switch (mode) {
    case HAL_I2S_MODE_MASTER_RX: *tx = false; return I2S_MODE_MASTER_RX;
    case HAL_I2S_MODE_SLAVE_TX:  *tx = true;  return I2S_MODE_SLAVE_TX;
    case HAL_I2S_MODE_SLAVE_RX:  *tx = false; return I2S_MODE_SLAVE_RX;
    default:                      *tx = true;  return I2S_MODE_MASTER_TX;
    }
}

static uint32_t stm32_i2s_standard(hal_i2s_standard_e standard)
{
    switch (standard) {
    case HAL_I2S_STANDARD_MSB: return I2S_STANDARD_MSB;
    case HAL_I2S_STANDARD_LSB: return I2S_STANDARD_LSB;
    case HAL_I2S_STANDARD_PCM: return I2S_STANDARD_PCM_SHORT;
    default:                    return I2S_STANDARD_PHILIPS;
    }
}

static uint32_t stm32_i2s_format(hal_i2s_data_size_e size)
{
    switch (size) {
    case HAL_I2S_DATA_24BIT: return I2S_DATAFORMAT_24B;
    case HAL_I2S_DATA_32BIT: return I2S_DATAFORMAT_32B;
    default:                  return I2S_DATAFORMAT_16B;
    }
}

static size_t stm32_i2s_sample_bytes(hal_i2s_data_size_e size)
{
    switch (size) {
    case HAL_I2S_DATA_24BIT: return 3U;
    case HAL_I2S_DATA_32BIT: return 4U;
    default:                  return 2U;
    }
}

int hal_i2s_init(hal_i2s_t *handle, const hal_i2s_config_t *config)
{
    stm32_slot_t *slot;
    uint8_t instance;
    HAL_StatusTypeDef status;
    I2S_HandleTypeDef *hi2s;
    bool tx_mode = false;

    if (handle == NULL || config == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (config->sample_rate == 0U) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state != HAL_STATE_RESET) {
        return HAL_ALREADY_INIT;
    }
    slot = stm32_slot_alloc(STM32_SLOT_I2S, (uint8_t)HAL_ARRAY_SIZE(s_i2s_bases), &instance);
    if (slot == NULL) {
        return HAL_NO_MEMORY;
    }
    hi2s = &slot->hw.i2s;
    memset(hi2s, 0, sizeof(*hi2s));
    hi2s->Instance = s_i2s_bases[instance];
    hi2s->Init.Mode = stm32_i2s_mode(config->mode, &tx_mode);
    HAL_UNUSED(tx_mode);
    hi2s->Init.Standard = stm32_i2s_standard(config->standard);
    hi2s->Init.DataFormat = stm32_i2s_format(config->data_size);
    hi2s->Init.MCLKOutput = config->master_clock ? I2S_MCLKOUTPUT_ENABLE
                                                 : I2S_MCLKOUTPUT_DISABLE;
    hi2s->Init.AudioFreq = config->sample_rate;
    hi2s->Init.CPOL = I2S_CPOL_LOW;
    hi2s->Init.ClockSource = I2S_CLKSOURCE_PLL;

    status = HAL_I2S_Init(hi2s);
    if (status != HAL_OK) {
        stm32_slot_free(slot);
        return stm32_from_hal(status);
    }
    slot->owner = handle;
    handle->config = *config;
    handle->callback = NULL;
    handle->user_data = NULL;
    handle->tx_dma = NULL;
    handle->rx_dma = NULL;
    return stm32_base_init(&handle->base, slot);
}

int hal_i2s_deinit(hal_i2s_t *handle)
{
    stm32_slot_t *slot;

    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    slot = (stm32_slot_t *)handle->base.instance;
    (void)HAL_I2S_DeInit(&slot->hw.i2s);
    stm32_slot_free(slot);
    stm32_base_reset(&handle->base);
    return HAL_OK;
}

int hal_i2s_transmit(hal_i2s_t *handle, const void *data,
                                size_t length, uint32_t timeout)
{
    stm32_slot_t *slot;
    size_t frames;
    HAL_StatusTypeDef status;

    if (handle == NULL || data == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    frames = length / stm32_i2s_sample_bytes(handle->config.data_size);
    if (frames == 0U || frames > 0xFFFFU) {
        return HAL_INVALID_PARAM;
    }
    slot = (stm32_slot_t *)handle->base.instance;
    handle->base.state = HAL_STATE_BUSY_TX;
    status = HAL_I2S_Transmit(&slot->hw.i2s, (uint16_t *)data, (uint16_t)frames,
                              stm32_timeout(timeout));
    handle->base.state = (status == HAL_OK) ? HAL_STATE_READY : HAL_STATE_ERROR;
    return stm32_from_hal(status);
}

int hal_i2s_receive(hal_i2s_t *handle, void *data,
                               size_t length, uint32_t timeout)
{
    stm32_slot_t *slot;
    size_t frames;
    HAL_StatusTypeDef status;

    if (handle == NULL || data == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    frames = length / stm32_i2s_sample_bytes(handle->config.data_size);
    if (frames == 0U || frames > 0xFFFFU) {
        return HAL_INVALID_PARAM;
    }
    slot = (stm32_slot_t *)handle->base.instance;
    handle->base.state = HAL_STATE_BUSY_RX;
    status = HAL_I2S_Receive(&slot->hw.i2s, (uint16_t *)data, (uint16_t)frames,
                             stm32_timeout(timeout));
    handle->base.state = (status == HAL_OK) ? HAL_STATE_READY : HAL_STATE_ERROR;
    return stm32_from_hal(status);
}

int hal_i2s_transmit_dma(hal_i2s_t *handle, const void *data,
                                    size_t length)
{
    stm32_slot_t *slot;
    size_t frames;
    HAL_StatusTypeDef status;

    if (handle == NULL || data == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    frames = length / stm32_i2s_sample_bytes(handle->config.data_size);
    if (frames == 0U || frames > 0xFFFFU) {
        return HAL_INVALID_PARAM;
    }
    slot = (stm32_slot_t *)handle->base.instance;
    status = HAL_I2S_Transmit_DMA(&slot->hw.i2s, (uint16_t *)data, (uint16_t)frames);
    if (status == HAL_OK) {
        handle->base.state = HAL_STATE_BUSY_TX;
    }
    return stm32_from_hal(status);
}

int hal_i2s_receive_dma(hal_i2s_t *handle, void *data,
                                   size_t length)
{
    stm32_slot_t *slot;
    size_t frames;
    HAL_StatusTypeDef status;

    if (handle == NULL || data == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    frames = length / stm32_i2s_sample_bytes(handle->config.data_size);
    if (frames == 0U || frames > 0xFFFFU) {
        return HAL_INVALID_PARAM;
    }
    slot = (stm32_slot_t *)handle->base.instance;
    status = HAL_I2S_Receive_DMA(&slot->hw.i2s, (uint16_t *)data, (uint16_t)frames);
    if (status == HAL_OK) {
        handle->base.state = HAL_STATE_BUSY_RX;
    }
    return stm32_from_hal(status);
}

int hal_i2s_abort(hal_i2s_t *handle)
{
    stm32_slot_t *slot;

    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    slot = (stm32_slot_t *)handle->base.instance;
    (void)HAL_I2S_DMAStop(&slot->hw.i2s);
    handle->base.state = HAL_STATE_READY;
    return HAL_OK;
}

/* ---------------- CAN / FDCAN ---------------- */
static uint32_t stm32_can_mode(hal_can_mode_e mode)
{
    switch (mode) {
    case HAL_CAN_MODE_LOOPBACK:        return FDCAN_MODE_INTERNAL_LOOPBACK;
    case HAL_CAN_MODE_SILENT:          return FDCAN_MODE_BUS_MONITORING;
    case HAL_CAN_MODE_SILENT_LOOPBACK: return FDCAN_MODE_EXTERNAL_LOOPBACK;
    default:                            return FDCAN_MODE_NORMAL;
    }
}

static uint32_t stm32_can_dlc_to_hal(uint8_t dlc)
{
    switch (dlc) {
    case 0U: return FDCAN_DLC_BYTES_0;
    case 1U: return FDCAN_DLC_BYTES_1;
    case 2U: return FDCAN_DLC_BYTES_2;
    case 3U: return FDCAN_DLC_BYTES_3;
    case 4U: return FDCAN_DLC_BYTES_4;
    case 5U: return FDCAN_DLC_BYTES_5;
    case 6U: return FDCAN_DLC_BYTES_6;
    case 7U: return FDCAN_DLC_BYTES_7;
    case 8U: return FDCAN_DLC_BYTES_8;
    case 12U: return FDCAN_DLC_BYTES_12;
    case 16U: return FDCAN_DLC_BYTES_16;
    case 20U: return FDCAN_DLC_BYTES_20;
    case 24U: return FDCAN_DLC_BYTES_24;
    case 32U: return FDCAN_DLC_BYTES_32;
    case 48U: return FDCAN_DLC_BYTES_48;
    case 64U: return FDCAN_DLC_BYTES_64;
    default: return FDCAN_DLC_BYTES_8;
    }
}

static uint8_t stm32_can_dlc_from_hal(uint32_t dlc)
{
    switch (dlc) {
    case FDCAN_DLC_BYTES_0: return 0U;
    case FDCAN_DLC_BYTES_1: return 1U;
    case FDCAN_DLC_BYTES_2: return 2U;
    case FDCAN_DLC_BYTES_3: return 3U;
    case FDCAN_DLC_BYTES_4: return 4U;
    case FDCAN_DLC_BYTES_5: return 5U;
    case FDCAN_DLC_BYTES_6: return 6U;
    case FDCAN_DLC_BYTES_7: return 7U;
    case FDCAN_DLC_BYTES_8: return 8U;
    case FDCAN_DLC_BYTES_12: return 12U;
    case FDCAN_DLC_BYTES_16: return 16U;
    case FDCAN_DLC_BYTES_20: return 20U;
    case FDCAN_DLC_BYTES_24: return 24U;
    case FDCAN_DLC_BYTES_32: return 32U;
    case FDCAN_DLC_BYTES_48: return 48U;
    case FDCAN_DLC_BYTES_64: return 64U;
    default: return 8U;
    }
}

static void stm32_can_bit_timing(uint32_t bitrate, uint32_t *prescaler,
                              uint32_t *tseg1, uint32_t *tseg2)
{
    /* H7 FDCAN 内核时钟默认 80 MHz，采样点约 87.5%。 */
    uint32_t p = (bitrate == 0U) ? 20U : (80000000U / (bitrate * 16U));
    if (p == 0U) {
        p = 1U;
    }
    if (p > 512U) {
        p = 512U;
    }
    *prescaler = p;
    *tseg1 = 13U;
    *tseg2 = 2U;
}

int hal_can_init(hal_can_t *handle, const hal_can_config_t *config)
{
    stm32_slot_t *slot;
    uint8_t instance;
    HAL_StatusTypeDef status;
    FDCAN_HandleTypeDef *hcan;
    uint32_t prescaler;
    uint32_t tseg1;
    uint32_t tseg2;

    if (handle == NULL || config == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (config->bitrate == 0U) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state != HAL_STATE_RESET) {
        return HAL_ALREADY_INIT;
    }
    slot = stm32_slot_alloc(STM32_SLOT_CAN, (uint8_t)HAL_ARRAY_SIZE(s_can_bases), &instance);
    if (slot == NULL) {
        return HAL_NO_MEMORY;
    }
    hcan = &slot->hw.can;
    memset(hcan, 0, sizeof(*hcan));
    hcan->Instance = s_can_bases[instance];
    stm32_can_bit_timing(config->bitrate, &prescaler, &tseg1, &tseg2);
    hcan->Init.FrameFormat = config->enable_fd ? FDCAN_FRAME_FD_BRS : FDCAN_FRAME_CLASSIC;
    hcan->Init.Mode = stm32_can_mode(config->mode);
    hcan->Init.AutoRetransmission = ENABLE;
    hcan->Init.TransmitPause = DISABLE;
    hcan->Init.ProtocolException = DISABLE;
    hcan->Init.NominalPrescaler = prescaler;
    hcan->Init.NominalSyncJumpWidth = 1U;
    hcan->Init.NominalTimeSeg1 = tseg1;
    hcan->Init.NominalTimeSeg2 = tseg2;
    hcan->Init.DataPrescaler = prescaler;
    hcan->Init.DataSyncJumpWidth = 1U;
    hcan->Init.DataTimeSeg1 = tseg1;
    hcan->Init.DataTimeSeg2 = tseg2;
    hcan->Init.MessageRAMOffset = 0U;
    hcan->Init.StdFiltersNbr = 1U;
    hcan->Init.ExtFiltersNbr = 0U;
    hcan->Init.RxFifo0ElmtsNbr = 8U;
    hcan->Init.RxFifo0ElmtSize = config->enable_fd ? FDCAN_DATA_BYTES_64
                                                   : FDCAN_DATA_BYTES_8;
    hcan->Init.RxFifo1ElmtsNbr = 0U;
    hcan->Init.RxFifo1ElmtSize = FDCAN_DATA_BYTES_8;
    hcan->Init.RxBuffersNbr = 0U;
    hcan->Init.RxBufferSize = FDCAN_DATA_BYTES_8;
    hcan->Init.TxEventsNbr = 0U;
    hcan->Init.TxBuffersNbr = 0U;
    hcan->Init.TxFifoQueueElmtsNbr = 8U;
    hcan->Init.TxFifoQueueMode = FDCAN_TX_FIFO_OPERATION;
    hcan->Init.TxElmtSize = config->enable_fd ? FDCAN_DATA_BYTES_64
                                              : FDCAN_DATA_BYTES_8;

    status = HAL_FDCAN_Init(hcan);
    if (status != HAL_OK) {
        stm32_slot_free(slot);
        return stm32_from_hal(status);
    }
    slot->owner = handle;
    handle->config = *config;
    handle->callback = NULL;
    handle->user_data = NULL;
    return stm32_base_init(&handle->base, slot);
}

int hal_can_deinit(hal_can_t *handle)
{
    stm32_slot_t *slot;

    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    slot = (stm32_slot_t *)handle->base.instance;
    (void)HAL_FDCAN_DeInit(&slot->hw.can);
    stm32_slot_free(slot);
    stm32_base_reset(&handle->base);
    return HAL_OK;
}

int hal_can_start(hal_can_t *handle)
{
    stm32_slot_t *slot;

    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    slot = (stm32_slot_t *)handle->base.instance;
    if (HAL_FDCAN_Start(&slot->hw.can) != HAL_OK) {
        return HAL_ERROR;
    }
    handle->base.state = HAL_STATE_READY;
    return HAL_OK;
}

int hal_can_stop(hal_can_t *handle)
{
    stm32_slot_t *slot;

    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    slot = (stm32_slot_t *)handle->base.instance;
    (void)HAL_FDCAN_Stop(&slot->hw.can);
    handle->base.state = HAL_STATE_READY;
    return HAL_OK;
}

int hal_can_transmit(hal_can_t *handle, const hal_can_frame_t *frame,
                                uint32_t timeout)
{
    stm32_slot_t *slot;
    FDCAN_TxHeaderTypeDef header;

    HAL_UNUSED(timeout);
    if (handle == NULL || frame == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    memset(&header, 0, sizeof(header));
    header.Identifier = frame->id;
    header.IdType = (frame->id_type == HAL_CAN_ID_EXTENDED) ? FDCAN_EXTENDED_ID
                                                             : FDCAN_STANDARD_ID;
    header.TxFrameType = (frame->frame_type == HAL_CAN_FRAME_REMOTE)
                         ? FDCAN_REMOTE_FRAME : FDCAN_DATA_FRAME;
    header.DataLength = stm32_can_dlc_to_hal(frame->dlc);
    header.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
    header.BitRateSwitch = frame->brs ? FDCAN_BRS_ON : FDCAN_BRS_OFF;
    header.FDFormat = frame->fd ? FDCAN_FD_CAN : FDCAN_CLASSIC_CAN;
    header.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
    header.MessageMarker = 0U;

    slot = (stm32_slot_t *)handle->base.instance;
    if (HAL_FDCAN_AddMessageToTxFifoQ(&slot->hw.can, &header,
                                      (uint8_t *)frame->data) != HAL_OK) {
        handle->base.error = HAL_BUSY;
        return HAL_BUSY;
    }
    return HAL_OK;
}

int hal_can_receive(hal_can_t *handle, hal_can_frame_t *frame,
                               uint32_t timeout)
{
    stm32_slot_t *slot;
    FDCAN_RxHeaderTypeDef header;

    HAL_UNUSED(timeout);
    if (handle == NULL || frame == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    if (frame->dlc > 64U) {
        return HAL_INVALID_PARAM;
    }
    slot = (stm32_slot_t *)handle->base.instance;
    memset(&header, 0, sizeof(header));
    if (HAL_FDCAN_GetRxMessage(&slot->hw.can, FDCAN_RX_FIFO0, &header,
                               frame->data) != HAL_OK) {
        return HAL_TIMEOUT;
    }
    frame->id = header.Identifier;
    frame->id_type = (header.IdType == FDCAN_EXTENDED_ID) ? HAL_CAN_ID_EXTENDED
                                                          : HAL_CAN_ID_STANDARD;
    frame->frame_type = (header.RxFrameType == FDCAN_REMOTE_FRAME)
                        ? HAL_CAN_FRAME_REMOTE : HAL_CAN_FRAME_DATA;
    frame->dlc = stm32_can_dlc_from_hal(header.DataLength);
    frame->fd = (header.FDFormat == FDCAN_FD_CAN);
    frame->brs = (header.BitRateSwitch == FDCAN_BRS_ON);
    return HAL_OK;
}

int hal_can_transmit_it(hal_can_t *handle,
                                   const hal_can_frame_t *frame)
{
    int status = hal_can_transmit(handle, frame, 0U);

    if (status == HAL_OK && handle != NULL) {
        stm32_slot_t *slot = (stm32_slot_t *)handle->base.instance;
        (void)HAL_FDCAN_ActivateNotification(&slot->hw.can, FDCAN_IT_TX_FIFO_EMPTY, 0U);
    }
    return status;
}

int hal_can_add_filter(hal_can_t *handle,
                                  const hal_can_filter_t *filter)
{
    stm32_slot_t *slot;
    FDCAN_FilterTypeDef hal_filter;

    if (handle == NULL || filter == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    memset(&hal_filter, 0, sizeof(hal_filter));
    hal_filter.IdType = (filter->id_type == HAL_CAN_ID_EXTENDED) ? FDCAN_EXTENDED_ID
                                                                  : FDCAN_STANDARD_ID;
    hal_filter.FilterIndex = 0U;
    hal_filter.FilterType = (filter->mask == 0U) ? FDCAN_FILTER_RANGE
                                                 : FDCAN_FILTER_MASK;
    hal_filter.FilterConfig = FDCAN_FILTER_TO_RXFIFO0;
    hal_filter.FilterID1 = filter->id;
    hal_filter.FilterID2 = (filter->mask == 0U) ? filter->id : filter->mask;

    slot = (stm32_slot_t *)handle->base.instance;
    return stm32_from_hal(HAL_FDCAN_ConfigFilter(&slot->hw.can, &hal_filter));
}

int hal_can_clear_filters(hal_can_t *handle)
{
    stm32_slot_t *slot;

    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    slot = (stm32_slot_t *)handle->base.instance;
    return stm32_from_hal(HAL_FDCAN_ConfigGlobalFilter(&slot->hw.can, FDCAN_REJECT,
                                                    FDCAN_REJECT, FDCAN_REJECT_REMOTE,
                                                    FDCAN_REJECT_REMOTE));
}

/* ---------------- ADC ---------------- */
static uint32_t stm32_adc_resolution(hal_adc_resolution_e resolution)
{
    switch (resolution) {
    case HAL_ADC_RESOLUTION_16BIT: return ADC_RESOLUTION_16B;
    case HAL_ADC_RESOLUTION_14BIT: return ADC_RESOLUTION_14B;
    case HAL_ADC_RESOLUTION_12BIT: return ADC_RESOLUTION_12B;
    case HAL_ADC_RESOLUTION_10BIT: return ADC_RESOLUTION_10B;
    default:                        return ADC_RESOLUTION_8B;
    }
}

int hal_adc_init(hal_adc_t *handle, const hal_adc_config_t *config)
{
    stm32_slot_t *slot;
    uint8_t instance;
    HAL_StatusTypeDef status;
    ADC_HandleTypeDef *hadc;

    if (handle == NULL || config == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (config->channel_count == 0U) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state != HAL_STATE_RESET) {
        return HAL_ALREADY_INIT;
    }
    slot = stm32_slot_alloc(STM32_SLOT_ADC, (uint8_t)HAL_ARRAY_SIZE(s_adc_bases), &instance);
    if (slot == NULL) {
        return HAL_NO_MEMORY;
    }
    hadc = &slot->hw.adc;
    memset(hadc, 0, sizeof(*hadc));
    hadc->Instance = s_adc_bases[instance];
    hadc->Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
    hadc->Init.Resolution = stm32_adc_resolution(config->resolution);
    hadc->Init.ScanConvMode = (config->channel_count > 1U) ? ENABLE : DISABLE;
    hadc->Init.EOCSelection = ADC_EOC_SINGLE_CONV;
    hadc->Init.ConversionDataManagement = ADC_CONVERSIONDATA_DR;
    hadc->Init.ContinuousConvMode = DISABLE;
    hadc->Init.DiscontinuousConvMode = DISABLE;
    hadc->Init.NbrOfConversion = config->channel_count;
    hadc->Init.DiscontinuousNbrConversion = 0U;
    hadc->Init.ExternalTrigConv = ADC_SOFTWARE_START;
    hadc->Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
    hadc->Init.Overrun = ADC_OVR_DATA_OVERWRITTEN;
    hadc->Init.LeftBitShift = ADC_LEFTBITSHIFT_NONE;
    hadc->Init.LowPowerAutoPowerOff = DISABLE;
    hadc->Init.GainCompensation = ADC_GAIN_COMPENSATION_DISABLE;
    hadc->Init.SamplingMode = ADC_SAMPLING_MODE_NORMAL;
    hadc->Init.TriggerFrequencyMode = ADC_TRIGGER_FREQ_HIGH;

    status = HAL_ADC_Init(hadc);
    if (status != HAL_OK) {
        stm32_slot_free(slot);
        return stm32_from_hal(status);
    }
    slot->owner = handle;
    handle->config = *config;
    handle->callback = NULL;
    handle->user_data = NULL;
    handle->dma = NULL;
    return stm32_base_init(&handle->base, slot);
}

int hal_adc_deinit(hal_adc_t *handle)
{
    stm32_slot_t *slot;

    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    slot = (stm32_slot_t *)handle->base.instance;
    (void)HAL_ADC_DeInit(&slot->hw.adc);
    stm32_slot_free(slot);
    stm32_base_reset(&handle->base);
    return HAL_OK;
}

int hal_adc_config_channel(hal_adc_t *handle,
                                      const hal_adc_channel_config_t *config)
{
    stm32_slot_t *slot;
    ADC_ChannelConfTypeDef channel;

    if (handle == NULL || config == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    memset(&channel, 0, sizeof(channel));
    channel.Channel = config->channel;
    channel.Rank = config->rank;
    channel.SamplingTime = (config->sample_time == 0U) ? ADC_SAMPLETIME_64CYCLES_5
                                                       : config->sample_time;
    channel.SingleDiff = ADC_SINGLE_ENDED;
    channel.OffsetNumber = ADC_OFFSET_NONE;
    channel.Offset = 0U;
    channel.OffsetSign = ADC_OFFSET_SIGN_NEGATIVE;
    channel.OffsetSaturation = DISABLE;

    slot = (stm32_slot_t *)handle->base.instance;
    return stm32_from_hal(HAL_ADC_ConfigChannel(&slot->hw.adc, &channel));
}

int hal_adc_start(hal_adc_t *handle)
{
    stm32_slot_t *slot;

    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    slot = (stm32_slot_t *)handle->base.instance;
    return stm32_from_hal(HAL_ADC_Start(&slot->hw.adc));
}

int hal_adc_stop(hal_adc_t *handle)
{
    stm32_slot_t *slot;

    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    slot = (stm32_slot_t *)handle->base.instance;
    return stm32_from_hal(HAL_ADC_Stop(&slot->hw.adc));
}

int hal_adc_read(hal_adc_t *handle, uint8_t channel,
                            uint32_t *value, uint32_t timeout)
{
    stm32_slot_t *slot;
    HAL_StatusTypeDef status;
    ADC_ChannelConfTypeDef config;

    if (handle == NULL || value == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    slot = (stm32_slot_t *)handle->base.instance;
    memset(&config, 0, sizeof(config));
    config.Channel = channel;
    config.Rank = ADC_REGULAR_RANK_1;
    config.SamplingTime = ADC_SAMPLETIME_64CYCLES_5;
    config.SingleDiff = ADC_SINGLE_ENDED;
    config.OffsetNumber = ADC_OFFSET_NONE;
    config.OffsetSign = ADC_OFFSET_SIGN_NEGATIVE;
    if (HAL_ADC_ConfigChannel(&slot->hw.adc, &config) != HAL_OK) {
        return HAL_ERROR;
    }
    if (HAL_ADC_Start(&slot->hw.adc) != HAL_OK) {
        return HAL_ERROR;
    }
    status = HAL_ADC_PollForConversion(&slot->hw.adc, stm32_timeout(timeout));
    if (status != HAL_OK) {
        (void)HAL_ADC_Stop(&slot->hw.adc);
        return stm32_from_hal(status);
    }
    *value = HAL_ADC_GetValue(&slot->hw.adc);
    (void)HAL_ADC_Stop(&slot->hw.adc);
    return HAL_OK;
}

int hal_adc_start_it(hal_adc_t *handle)
{
    stm32_slot_t *slot;

    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    slot = (stm32_slot_t *)handle->base.instance;
    return stm32_from_hal(HAL_ADC_Start_IT(&slot->hw.adc));
}

int hal_adc_start_dma(hal_adc_t *handle, uint32_t *buffer,
                                 size_t length)
{
    stm32_slot_t *slot;

    if (handle == NULL || buffer == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    if (length == 0U) {
        return HAL_INVALID_PARAM;
    }
    slot = (stm32_slot_t *)handle->base.instance;
    return stm32_from_hal(HAL_ADC_Start_DMA(&slot->hw.adc, buffer, (uint32_t)length));
}

int hal_adc_calibrate(hal_adc_t *handle)
{
    stm32_slot_t *slot;

    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    slot = (stm32_slot_t *)handle->base.instance;
    return stm32_from_hal(HAL_ADCEx_Calibration_Start(&slot->hw.adc,
                                                   ADC_CALIB_OFFSET_LINEARITY,
                                                   ADC_SINGLE_ENDED));
}

uint32_t hal_adc_get_max_value(hal_adc_t *handle)
{
    uint32_t bits;

    if (handle == NULL || handle->base.state == HAL_STATE_RESET) {
        return 0U;
    }
    bits = (uint32_t)handle->config.resolution;
    if (bits >= 32U) {
        return 0xFFFFFFFFU;
    }
    return (1UL << bits) - 1UL;
}

/* ---------------- DAC ---------------- */
static DAC_HandleTypeDef s_dac;
static bool s_dac_initialized = false;
static bool s_dac_channel_used[2] = { false, false };

static uint32_t stm32_dac_channel(uint8_t channel)
{
    return (channel == 0U) ? DAC_CHANNEL_1 : DAC_CHANNEL_2;
}

static uint32_t stm32_dac_alignment(hal_dac_alignment_e alignment)
{
    switch (alignment) {
    case HAL_DAC_ALIGN_8BIT:        return DAC_ALIGN_8B_R;
    case HAL_DAC_ALIGN_12BIT_LEFT:  return DAC_ALIGN_12B_L;
    default:                         return DAC_ALIGN_12B_R;
    }
}

int hal_dac_init(hal_dac_t *handle, const hal_dac_config_t *config)
{
    DAC_ChannelConfTypeDef channel;
    HAL_StatusTypeDef status;

    if (handle == NULL || config == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (config->channel > 1U) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state != HAL_STATE_RESET) {
        return HAL_ALREADY_INIT;
    }
    if (s_dac_channel_used[config->channel]) {
        return HAL_ALREADY_INIT;
    }
    if (!s_dac_initialized) {
        memset(&s_dac, 0, sizeof(s_dac));
        s_dac.Instance = DAC1;
        status = HAL_DAC_Init(&s_dac);
        if (status != HAL_OK) {
            return stm32_from_hal(status);
        }
        s_dac_initialized = true;
    }
    memset(&channel, 0, sizeof(channel));
    channel.DAC_Trigger = DAC_TRIGGER_NONE;
    channel.DAC_OutputBuffer = DAC_OUTPUTBUFFER_ENABLE;
    channel.DAC_ConnectOnChipPeripheral = DAC_CHIPCONNECT_DISABLE;
    channel.DAC_UserTrimming = DAC_TRIMMING_FACTORY;
    channel.DAC_TrimmingValue = 0U;
    channel.DAC_SampleAndHold = DAC_SAMPLEANDHOLD_DISABLE;
    channel.DAC_SampleTime = 0U;
    channel.DAC_SampleAndHoldRefreshOption = 0U;
    channel.DAC_LFSRUnmask_Bits = 0U;
    channel.DAC_TriangleAmplitude = 0U;
    channel.DAC_WaveGeneration = DAC_WAVEGENERATION_NONE;
    channel.DAC_HighFrequency = DAC_HIGH_FREQUENCY_INTERFACE_MODE_DISABLE;
    status = HAL_DAC_ConfigChannel(&s_dac, &channel, stm32_dac_channel(config->channel));
    if (status != HAL_OK) {
        return stm32_from_hal(status);
    }
    s_dac_channel_used[config->channel] = true;
    handle->config = *config;
    handle->dma = NULL;
    return stm32_base_init(&handle->base, &s_dac);
}

int hal_dac_deinit(hal_dac_t *handle)
{
    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    (void)HAL_DAC_Stop(&s_dac, stm32_dac_channel(handle->config.channel));
    s_dac_channel_used[handle->config.channel] = false;
    stm32_base_reset(&handle->base);
    return HAL_OK;
}

int hal_dac_start(hal_dac_t *handle)
{
    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    return stm32_from_hal(HAL_DAC_Start(&s_dac, stm32_dac_channel(handle->config.channel)));
}

int hal_dac_stop(hal_dac_t *handle)
{
    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    return stm32_from_hal(HAL_DAC_Stop(&s_dac, stm32_dac_channel(handle->config.channel)));
}

int hal_dac_write(hal_dac_t *handle, uint32_t value)
{
    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    return stm32_from_hal(HAL_DAC_SetValue(&s_dac, stm32_dac_channel(handle->config.channel),
                                        stm32_dac_alignment(handle->config.alignment),
                                        value));
}

int hal_dac_write_dma(hal_dac_t *handle, const uint32_t *buffer,
                                 size_t length)
{
    if (handle == NULL || buffer == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    if (length == 0U) {
        return HAL_INVALID_PARAM;
    }
    return stm32_from_hal(HAL_DAC_Start_DMA(&s_dac, stm32_dac_channel(handle->config.channel),
                                         (uint32_t *)buffer, (uint32_t)length,
                                         stm32_dac_alignment(handle->config.alignment)));
}

/* ---------------- OPAMP ---------------- */
#if defined(OPAMP1)
static uint32_t stm32_opamp_gain(uint8_t gain)
{
    switch (gain) {
    case 4U:  return OPAMP_PGA_GAIN_4_OR_MINUS_3;
    case 8U:  return OPAMP_PGA_GAIN_8_OR_MINUS_7;
    case 16U: return OPAMP_PGA_GAIN_16_OR_MINUS_15;
    case 32U: return OPAMP_PGA_GAIN_32_OR_MINUS_31;
    case 64U: return OPAMP_PGA_GAIN_64_OR_MINUS_63;
    default:  return OPAMP_PGA_GAIN_2_OR_MINUS_1;
    }
}
#endif

int hal_opamp_init(hal_opamp_t *handle,
                              const hal_opamp_config_t *config)
{
#if defined(OPAMP1)
    stm32_slot_t *slot;
    uint8_t instance;
    OPAMP_InitTypeDef init;

    if (handle == NULL || config == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state != HAL_STATE_RESET) {
        return HAL_ALREADY_INIT;
    }
    slot = stm32_slot_alloc(STM32_SLOT_OPAMP, 2U, &instance);
    if (slot == NULL) {
        return HAL_NO_MEMORY;
    }
    memset(&slot->hw.opamp, 0, sizeof(slot->hw.opamp));
    slot->hw.opamp.Instance = (instance == 0U) ? OPAMP1 : OPAMP2;

    memset(&init, 0, sizeof(init));
    init.PowerMode = OPAMP_POWERMODE_NORMAL;
    init.Mode = (config->mode == HAL_OPAMP_MODE_PGA) ? OPAMP_MODE_PGA
                                                      : OPAMP_MODE_FOLLOWER;
    init.InvertingInput = config->negative_input;
    init.NonInvertingInput = config->positive_input;
    init.PgaGain = stm32_opamp_gain(config->gain);
    init.UserTrimming = OPAMP_TRIMMING_FACTORY;
    init.TrimmingValue = 0U;
    if (HAL_OPAMP_Init(&slot->hw.opamp, &init) != HAL_OK) {
        stm32_slot_free(slot);
        return HAL_ERROR;
    }
    slot->owner = handle;
    handle->config = *config;
    return stm32_base_init(&handle->base, slot);
#else
    HAL_UNUSED(handle);
    HAL_UNUSED(config);
    return HAL_NOT_SUPPORTED;
#endif
}

int hal_opamp_start(hal_opamp_t *handle)
{
#if defined(OPAMP1)
    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    return stm32_from_hal(HAL_OPAMP_Start(&((stm32_slot_t *)handle->base.instance)->hw.opamp));
#else
    HAL_UNUSED(handle);
    return HAL_NOT_SUPPORTED;
#endif
}

int hal_opamp_stop(hal_opamp_t *handle)
{
#if defined(OPAMP1)
    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    return stm32_from_hal(HAL_OPAMP_Stop(&((stm32_slot_t *)handle->base.instance)->hw.opamp));
#else
    HAL_UNUSED(handle);
    return HAL_NOT_SUPPORTED;
#endif
}

/* ---------------- TIM ---------------- */
static uint32_t stm32_tim_clock(uint8_t instance)
{
    switch (instance) {
    case 0U:    /* TIM1  */
    case 7U:    /* TIM8  */
    case 9U:    /* TIM15 */
    case 10U:   /* TIM16 */
    case 11U:   /* TIM17 */
        return (uint32_t)HAL_RCC_GetPCLK2Freq() * 2U;
    default:
        return (uint32_t)HAL_RCC_GetPCLK1Freq() * 2U;
    }
}

static void stm32_tim_clock_enable(uint8_t instance)
{
    switch (instance) {
    case 0U:  __HAL_RCC_TIM1_CLK_ENABLE(); break;
    case 1U:  __HAL_RCC_TIM2_CLK_ENABLE(); break;
    case 2U:  __HAL_RCC_TIM3_CLK_ENABLE(); break;
    case 3U:  __HAL_RCC_TIM4_CLK_ENABLE(); break;
    case 4U:  __HAL_RCC_TIM5_CLK_ENABLE(); break;
    case 5U:  __HAL_RCC_TIM6_CLK_ENABLE(); break;
    case 6U:  __HAL_RCC_TIM7_CLK_ENABLE(); break;
    case 7U:  __HAL_RCC_TIM8_CLK_ENABLE(); break;
    case 8U:  __HAL_RCC_TIM12_CLK_ENABLE(); break;
    case 9U:  __HAL_RCC_TIM15_CLK_ENABLE(); break;
    case 10U: __HAL_RCC_TIM16_CLK_ENABLE(); break;
    case 11U: __HAL_RCC_TIM17_CLK_ENABLE(); break;
    default: break;
    }
}

static uint32_t stm32_tim_counter_mode(hal_tim_counter_e mode)
{
    switch (mode) {
    case HAL_TIM_COUNTER_DOWN:   return TIM_COUNTERMODE_DOWN;
    case HAL_TIM_COUNTER_CENTER: return TIM_COUNTERMODE_CENTERALIGNED1;
    default:                      return TIM_COUNTERMODE_UP;
    }
}

static uint32_t stm32_tim_clock_division(uint8_t division)
{
    switch (division) {
    case 2U: return TIM_CLOCKDIVISION_DIV2;
    case 4U: return TIM_CLOCKDIVISION_DIV4;
    default: return TIM_CLOCKDIVISION_DIV1;
    }
}

int hal_tim_init(hal_tim_t *handle, const hal_tim_config_t *config)
{
    stm32_slot_t *slot;
    uint8_t instance;
    TIM_HandleTypeDef *htim;

    if (handle == NULL || config == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state != HAL_STATE_RESET) {
        return HAL_ALREADY_INIT;
    }
    slot = stm32_slot_alloc(STM32_SLOT_TIM, (uint8_t)HAL_ARRAY_SIZE(s_tim_bases), &instance);
    if (slot == NULL) {
        return HAL_NO_MEMORY;
    }
    stm32_tim_clock_enable(instance);
    htim = &slot->hw.tim;
    memset(htim, 0, sizeof(*htim));
    htim->Instance = s_tim_bases[instance];
    htim->Init.Prescaler = config->prescaler;
    htim->Init.CounterMode = stm32_tim_counter_mode(config->counter_mode);
    htim->Init.Period = config->period;
    htim->Init.ClockDivision = stm32_tim_clock_division(config->clock_division);
    htim->Init.RepetitionCounter = 0U;
    htim->Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    if (HAL_TIM_Base_Init(htim) != HAL_OK) {
        stm32_slot_free(slot);
        return HAL_ERROR;
    }
    slot->owner = handle;
    handle->config = *config;
    handle->callback = NULL;
    handle->user_data = NULL;
    return stm32_base_init(&handle->base, slot);
}

int hal_tim_deinit(hal_tim_t *handle)
{
    stm32_slot_t *slot;

    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    slot = (stm32_slot_t *)handle->base.instance;
    (void)HAL_TIM_Base_Stop(&slot->hw.tim);
    (void)HAL_TIM_Base_DeInit(&slot->hw.tim);
    stm32_slot_free(slot);
    stm32_base_reset(&handle->base);
    return HAL_OK;
}

int hal_tim_start(hal_tim_t *handle)
{
    stm32_slot_t *slot;

    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    slot = (stm32_slot_t *)handle->base.instance;
    return stm32_from_hal(HAL_TIM_Base_Start(&slot->hw.tim));
}

int hal_tim_stop(hal_tim_t *handle)
{
    stm32_slot_t *slot;

    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    slot = (stm32_slot_t *)handle->base.instance;
    return stm32_from_hal(HAL_TIM_Base_Stop(&slot->hw.tim));
}

int hal_tim_start_it(hal_tim_t *handle)
{
    stm32_slot_t *slot;

    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    slot = (stm32_slot_t *)handle->base.instance;
    return stm32_from_hal(HAL_TIM_Base_Start_IT(&slot->hw.tim));
}

int hal_tim_stop_it(hal_tim_t *handle)
{
    stm32_slot_t *slot;

    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    slot = (stm32_slot_t *)handle->base.instance;
    return stm32_from_hal(HAL_TIM_Base_Stop_IT(&slot->hw.tim));
}

int hal_tim_set_period(hal_tim_t *handle, uint32_t period)
{
    stm32_slot_t *slot;

    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    slot = (stm32_slot_t *)handle->base.instance;
    __HAL_TIM_SET_AUTORELOAD(&slot->hw.tim, period);
    handle->config.period = period;
    return HAL_OK;
}

int hal_tim_set_prescaler(hal_tim_t *handle, uint32_t prescaler)
{
    stm32_slot_t *slot;

    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    slot = (stm32_slot_t *)handle->base.instance;
    __HAL_TIM_SET_PRESCALER(&slot->hw.tim, prescaler);
    handle->config.prescaler = prescaler;
    return HAL_OK;
}

int hal_tim_set_counter(hal_tim_t *handle, uint32_t counter)
{
    stm32_slot_t *slot;

    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    slot = (stm32_slot_t *)handle->base.instance;
    __HAL_TIM_SET_COUNTER(&slot->hw.tim, counter);
    return HAL_OK;
}

uint32_t hal_tim_get_counter(hal_tim_t *handle)
{
    stm32_slot_t *slot;

    if (handle == NULL || handle->base.state == HAL_STATE_RESET) {
        return 0U;
    }
    slot = (stm32_slot_t *)handle->base.instance;
    return (slot != NULL) ? (uint32_t)__HAL_TIM_GET_COUNTER(&slot->hw.tim) : 0U;
}

/* ---------------- PWM ---------------- */
static void stm32_pwm_apply_channel(stm32_slot_t *slot, uint8_t channel,
                                 hal_pwm_polarity_e polarity, uint32_t pulse)
{
    TIM_OC_InitTypeDef oc;

    memset(&oc, 0, sizeof(oc));
    oc.OCMode = TIM_OCMODE_PWM1;
    oc.Pulse = pulse;
    oc.OCPolarity = (polarity == HAL_PWM_POLARITY_LOW) ? TIM_OCPOLARITY_LOW
                                                         : TIM_OCPOLARITY_HIGH;
    oc.OCNPolarity = TIM_OCNPOLARITY_HIGH;
    oc.OCFastMode = TIM_OCFAST_DISABLE;
    oc.OCIdleState = TIM_OCIDLESTATE_RESET;
    oc.OCNIdleState = TIM_OCNIDLESTATE_RESET;
    (void)HAL_TIM_PWM_ConfigChannel(&slot->hw.tim, &oc,
                                    (uint32_t)(TIM_CHANNEL_1 << (channel - 1U)));
}

static void stm32_pwm_calc(uint32_t tim_clk, uint32_t frequency, uint32_t *prescaler,
                        uint32_t *period)
{
    uint32_t total = (frequency == 0U) ? 0U : (tim_clk / frequency);
    uint32_t psc;

    if (total < 2U) {
        total = 2U;
    }
    psc = total / 10000U;
    if (psc == 0U) {
        *prescaler = 0U;
        *period = (total > 65536U) ? 65535U : (total - 1U);
    } else if (psc > 65535U) {
        *prescaler = 65535U;
        *period = (total / 65536U > 0U && total / 65536U <= 65536U)
                  ? (total / 65536U - 1U) : 65535U;
    } else {
        *prescaler = psc;
        *period = 9999U;
    }
}

int hal_pwm_init(hal_pwm_t *handle, const hal_pwm_config_t *config)
{
    stm32_slot_t *slot;
    uint8_t instance;
    TIM_HandleTypeDef *htim;
    uint32_t prescaler;
    uint32_t period;
    uint32_t pulse;

    if (handle == NULL || config == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (config->frequency == 0U || config->duty > 10000U) {
        return HAL_INVALID_PARAM;
    }
    if (handle->channel < 1U || handle->channel > 4U) {
        handle->channel = 1U;
    }
    if (handle->base.state != HAL_STATE_RESET) {
        return HAL_ALREADY_INIT;
    }
    slot = stm32_slot_alloc(STM32_SLOT_PWM, (uint8_t)HAL_ARRAY_SIZE(s_tim_bases), &instance);
    if (slot == NULL) {
        return HAL_NO_MEMORY;
    }
    stm32_tim_clock_enable(instance);
    stm32_pwm_calc(stm32_tim_clock(instance), config->frequency, &prescaler, &period);
    pulse = (uint32_t)(((uint64_t)config->duty * (period + 1U)) / 10000U);

    htim = &slot->hw.tim;
    memset(htim, 0, sizeof(*htim));
    htim->Instance = s_tim_bases[instance];
    htim->Init.Prescaler = prescaler;
    htim->Init.CounterMode = TIM_COUNTERMODE_UP;
    htim->Init.Period = period;
    htim->Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim->Init.RepetitionCounter = 0U;
    htim->Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    if (HAL_TIM_PWM_Init(htim) != HAL_OK) {
        stm32_slot_free(slot);
        return HAL_ERROR;
    }
    stm32_pwm_apply_channel(slot, handle->channel, config->polarity, pulse);
    slot->owner = handle;
    handle->config = *config;
    return stm32_base_init(&handle->base, slot);
}

int hal_pwm_deinit(hal_pwm_t *handle)
{
    stm32_slot_t *slot;

    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    slot = (stm32_slot_t *)handle->base.instance;
    (void)HAL_TIM_PWM_Stop(&slot->hw.tim,
                           (uint32_t)(TIM_CHANNEL_1 << (handle->channel - 1U)));
    (void)HAL_TIM_PWM_DeInit(&slot->hw.tim);
    stm32_slot_free(slot);
    stm32_base_reset(&handle->base);
    return HAL_OK;
}

int hal_pwm_start(hal_pwm_t *handle)
{
    stm32_slot_t *slot;

    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    slot = (stm32_slot_t *)handle->base.instance;
    return stm32_from_hal(HAL_TIM_PWM_Start(&slot->hw.tim,
                                         (uint32_t)(TIM_CHANNEL_1 << (handle->channel - 1U))));
}

int hal_pwm_stop(hal_pwm_t *handle)
{
    stm32_slot_t *slot;

    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    slot = (stm32_slot_t *)handle->base.instance;
    return stm32_from_hal(HAL_TIM_PWM_Stop(&slot->hw.tim,
                                        (uint32_t)(TIM_CHANNEL_1 << (handle->channel - 1U))));
}

int hal_pwm_set_frequency(hal_pwm_t *handle, uint32_t frequency)
{
    stm32_slot_t *slot;
    uint32_t prescaler;
    uint32_t period;
    uint32_t pulse;

    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    if (frequency == 0U) {
        return HAL_INVALID_PARAM;
    }
    slot = (stm32_slot_t *)handle->base.instance;
    stm32_pwm_calc(stm32_tim_clock(slot->index), frequency, &prescaler, &period);
    __HAL_TIM_SET_PRESCALER(&slot->hw.tim, prescaler);
    __HAL_TIM_SET_AUTORELOAD(&slot->hw.tim, period);
    pulse = (uint32_t)(((uint64_t)handle->config.duty * (period + 1U)) / 10000U);
    __HAL_TIM_SET_COMPARE(&slot->hw.tim,
                          (uint32_t)(TIM_CHANNEL_1 << (handle->channel - 1U)), pulse);
    handle->config.frequency = frequency;
    return HAL_OK;
}

int hal_pwm_set_duty(hal_pwm_t *handle, uint32_t duty)
{
    stm32_slot_t *slot;
    uint32_t period;
    uint32_t pulse;

    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    if (duty > 10000U) {
        return HAL_INVALID_PARAM;
    }
    slot = (stm32_slot_t *)handle->base.instance;
    period = (uint32_t)__HAL_TIM_GET_AUTORELOAD(&slot->hw.tim);
    pulse = (uint32_t)(((uint64_t)duty * (period + 1U)) / 10000U);
    __HAL_TIM_SET_COMPARE(&slot->hw.tim,
                          (uint32_t)(TIM_CHANNEL_1 << (handle->channel - 1U)), pulse);
    handle->config.duty = duty;
    return HAL_OK;
}

int hal_pwm_set_polarity(hal_pwm_t *handle,
                                    hal_pwm_polarity_e polarity)
{
    stm32_slot_t *slot;
    uint32_t channel;

    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    slot = (stm32_slot_t *)handle->base.instance;
    channel = (uint32_t)(TIM_CHANNEL_1 << (handle->channel - 1U));
    (void)HAL_TIM_PWM_Stop(&slot->hw.tim, channel);
    stm32_pwm_apply_channel(slot, handle->channel, polarity,
                         __HAL_TIM_GET_COMPARE(&slot->hw.tim, channel));
    handle->config.polarity = polarity;
    return stm32_from_hal(HAL_TIM_PWM_Start(&slot->hw.tim, channel));
}

/* ---------------- CAPTURE ---------------- */
static uint32_t stm32_capture_polarity(hal_capture_edge_e edge)
{
    switch (edge) {
    case HAL_CAPTURE_FALLING: return TIM_INPUTCHANNELPOLARITY_FALLING;
    case HAL_CAPTURE_BOTH:    return TIM_INPUTCHANNELPOLARITY_BOTHEDGE;
    default:                   return TIM_INPUTCHANNELPOLARITY_RISING;
    }
}

static uint32_t stm32_capture_prescaler(uint32_t prescaler)
{
    switch (prescaler) {
    case 2U: return TIM_ICPSC_DIV2;
    case 4U: return TIM_ICPSC_DIV4;
    case 8U: return TIM_ICPSC_DIV8;
    default: return TIM_ICPSC_DIV1;
    }
}

int hal_capture_init(hal_capture_t *handle,
                                const hal_capture_config_t *config)
{
    stm32_slot_t *slot;
    uint8_t instance;
    TIM_HandleTypeDef *htim;
    TIM_IC_InitTypeDef ic;

    if (handle == NULL || config == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state != HAL_STATE_RESET) {
        return HAL_ALREADY_INIT;
    }
    slot = stm32_slot_alloc(STM32_SLOT_CAPTURE, (uint8_t)HAL_ARRAY_SIZE(s_tim_bases), &instance);
    if (slot == NULL) {
        return HAL_NO_MEMORY;
    }
    stm32_tim_clock_enable(instance);
    htim = &slot->hw.tim;
    memset(htim, 0, sizeof(*htim));
    htim->Instance = s_tim_bases[instance];
    htim->Init.Prescaler = config->prescaler;
    htim->Init.CounterMode = TIM_COUNTERMODE_UP;
    htim->Init.Period = 0xFFFFU;
    htim->Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim->Init.RepetitionCounter = 0U;
    htim->Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    if (HAL_TIM_IC_Init(htim) != HAL_OK) {
        stm32_slot_free(slot);
        return HAL_ERROR;
    }
    memset(&ic, 0, sizeof(ic));
    ic.ICPolarity = stm32_capture_polarity(config->edge);
    ic.ICSelection = TIM_ICSELECTION_DIRECTTI;
    ic.ICPrescaler = stm32_capture_prescaler(config->prescaler);
    ic.ICFilter = (uint32_t)(config->filter & 0x0FU);
    if (HAL_TIM_IC_ConfigChannel(htim, &ic, TIM_CHANNEL_1) != HAL_OK) {
        stm32_slot_free(slot);
        return HAL_ERROR;
    }
    slot->owner = handle;
    handle->config = *config;
    handle->callback = NULL;
    handle->user_data = NULL;
    return stm32_base_init(&handle->base, slot);
}

int hal_capture_start(hal_capture_t *handle)
{
    stm32_slot_t *slot;

    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    slot = (stm32_slot_t *)handle->base.instance;
    return stm32_from_hal(HAL_TIM_IC_Start(&slot->hw.tim, TIM_CHANNEL_1));
}

int hal_capture_stop(hal_capture_t *handle)
{
    stm32_slot_t *slot;

    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    slot = (stm32_slot_t *)handle->base.instance;
    return stm32_from_hal(HAL_TIM_IC_Stop(&slot->hw.tim, TIM_CHANNEL_1));
}

int hal_capture_start_it(hal_capture_t *handle)
{
    stm32_slot_t *slot;

    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    slot = (stm32_slot_t *)handle->base.instance;
    return stm32_from_hal(HAL_TIM_IC_Start_IT(&slot->hw.tim, TIM_CHANNEL_1));
}

uint32_t hal_capture_get_value(hal_capture_t *handle)
{
    stm32_slot_t *slot;

    if (handle == NULL || handle->base.state == HAL_STATE_RESET) {
        return 0U;
    }
    slot = (stm32_slot_t *)handle->base.instance;
    return (slot != NULL) ? HAL_TIM_ReadCapturedValue(&slot->hw.tim, TIM_CHANNEL_1) : 0U;
}

/* ---------------- ENCODER ---------------- */
static uint32_t stm32_encoder_mode(hal_encoder_mode_e mode)
{
    switch (mode) {
    case HAL_ENCODER_MODE_TI1:  return TIM_ENCODERMODE_TI1;
    case HAL_ENCODER_MODE_TI2:  return TIM_ENCODERMODE_TI2;
    default:                     return TIM_ENCODERMODE_TI12;
    }
}

int hal_encoder_init(hal_encoder_t *handle,
                                const hal_encoder_config_t *config)
{
    stm32_slot_t *slot;
    uint8_t instance;
    TIM_HandleTypeDef *htim;
    TIM_Encoder_InitTypeDef enc;
    uint32_t polarity;

    if (handle == NULL || config == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state != HAL_STATE_RESET) {
        return HAL_ALREADY_INIT;
    }
    slot = stm32_slot_alloc(STM32_SLOT_ENCODER, (uint8_t)HAL_ARRAY_SIZE(s_tim_bases), &instance);
    if (slot == NULL) {
        return HAL_NO_MEMORY;
    }
    stm32_tim_clock_enable(instance);
    polarity = config->reverse ? TIM_INPUTCHANNELPOLARITY_FALLING
                               : TIM_INPUTCHANNELPOLARITY_RISING;
    htim = &slot->hw.tim;
    memset(htim, 0, sizeof(*htim));
    htim->Instance = s_tim_bases[instance];
    htim->Init.Prescaler = 0U;
    htim->Init.CounterMode = TIM_COUNTERMODE_UP;
    htim->Init.Period = (config->period == 0U) ? 0xFFFFU : config->period;
    htim->Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim->Init.RepetitionCounter = 0U;
    htim->Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    memset(&enc, 0, sizeof(enc));
    enc.EncoderMode = stm32_encoder_mode(config->mode);
    enc.IC1Polarity = polarity;
    enc.IC1Selection = TIM_ICSELECTION_DIRECTTI;
    enc.IC1Prescaler = TIM_ICPSC_DIV1;
    enc.IC1Filter = (uint32_t)(config->filter & 0x0FU);
    enc.IC2Polarity = polarity;
    enc.IC2Selection = TIM_ICSELECTION_DIRECTTI;
    enc.IC2Prescaler = TIM_ICPSC_DIV1;
    enc.IC2Filter = (uint32_t)(config->filter & 0x0FU);
    if (HAL_TIM_Encoder_Init(htim, &enc) != HAL_OK) {
        stm32_slot_free(slot);
        return HAL_ERROR;
    }
    slot->owner = handle;
    handle->config = *config;
    return stm32_base_init(&handle->base, slot);
}

int hal_encoder_start(hal_encoder_t *handle)
{
    stm32_slot_t *slot;

    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    slot = (stm32_slot_t *)handle->base.instance;
    return stm32_from_hal(HAL_TIM_Encoder_Start(&slot->hw.tim, TIM_CHANNEL_ALL));
}

int hal_encoder_stop(hal_encoder_t *handle)
{
    stm32_slot_t *slot;

    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    slot = (stm32_slot_t *)handle->base.instance;
    return stm32_from_hal(HAL_TIM_Encoder_Stop(&slot->hw.tim, TIM_CHANNEL_ALL));
}

int32_t hal_encoder_get_count(hal_encoder_t *handle)
{
    stm32_slot_t *slot;

    if (handle == NULL || handle->base.state == HAL_STATE_RESET) {
        return 0;
    }
    slot = (stm32_slot_t *)handle->base.instance;
    return (slot != NULL) ? (int32_t)__HAL_TIM_GET_COUNTER(&slot->hw.tim) : 0;
}

int hal_encoder_set_count(hal_encoder_t *handle, int32_t count)
{
    stm32_slot_t *slot;

    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    slot = (stm32_slot_t *)handle->base.instance;
    __HAL_TIM_SET_COUNTER(&slot->hw.tim, (uint32_t)count);
    return HAL_OK;
}

/* ---------------- WDG ---------------- */
static uint32_t stm32_iwdg_prescaler(uint32_t timeout_ms, uint32_t *reload)
{
    static const uint32_t prescalers[] = { 4U, 8U, 16U, 32U, 64U, 128U, 256U };
    static const uint32_t values[] = {
        IWDG_PRESCALER_4, IWDG_PRESCALER_8, IWDG_PRESCALER_16, IWDG_PRESCALER_32,
        IWDG_PRESCALER_64, IWDG_PRESCALER_128, IWDG_PRESCALER_256
    };
    uint8_t i;

    /* LSI 约 32 kHz：reload = timeout(ms) * 32 / prescaler - 1 */
    for (i = 0U; i < HAL_ARRAY_SIZE(prescalers); i++) {
        uint32_t count = (timeout_ms * 32U) / prescalers[i];
        if (count > 0U && count <= 4096U) {
            *reload = count - 1U;
            return values[i];
        }
    }
    *reload = 4095U;
    return IWDG_PRESCALER_256;
}

int hal_wdg_init(hal_wdg_t *handle, const hal_wdg_config_t *config)
{
    uint32_t reload;

    if (handle == NULL || config == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (config->type == HAL_WDG_WINDOW) {
        /* 窗口看门狗需使用 WWDG，本实现只提供独立看门狗。 */
        return HAL_NOT_SUPPORTED;
    }
    if (handle->base.state != HAL_STATE_RESET) {
        return HAL_ALREADY_INIT;
    }
    memset(&s_iwdg, 0, sizeof(s_iwdg));
    s_iwdg.Instance = IWDG;
    s_iwdg.Init.Prescaler = stm32_iwdg_prescaler(config->timeout_ms, &reload);
    s_iwdg.Init.Reload = reload;
    s_iwdg.Init.Window = IWDG_WINDOW_DISABLE;
    if (HAL_IWDG_Init(&s_iwdg) != HAL_OK) {
        return HAL_ERROR;
    }
    s_iwdg_initialized = true;
    handle->config = *config;
    return stm32_base_init(&handle->base, &s_iwdg);
}

int hal_wdg_start(hal_wdg_t *handle)
{
    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    if (!s_iwdg_initialized) {
        return HAL_NOT_READY;
    }
    return stm32_from_hal(HAL_IWDG_Start(&s_iwdg));
}

int hal_wdg_refresh(hal_wdg_t *handle)
{
    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    if (!s_iwdg_initialized) {
        return HAL_NOT_READY;
    }
    return stm32_from_hal(HAL_IWDG_Refresh(&s_iwdg));
}

int hal_wdg_stop(hal_wdg_t *handle)
{
    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    /* IWDG 一旦启动无法停止，只能复位芯片。 */
    return HAL_NOT_SUPPORTED;
}

bool hal_wdg_is_reset(void)
{
    return (__HAL_RCC_GET_FLAG(RCC_FLAG_IWDGRST) != 0U);
}

void hal_wdg_clear_reset_flag(void)
{
    __HAL_RCC_CLEAR_RESET_FLAGS();
}

/* ---------------- RTC ---------------- */
static uint32_t stm32_epoch_days(uint16_t year, uint8_t month, uint8_t day)
{
    int y = (int)year;
    int m = (int)month;
    int era;
    unsigned yoe;
    unsigned doy;
    unsigned doe;
    int d;

    y -= (m <= 2) ? 1 : 0;
    era = (y >= 0 ? y : (y - 399)) / 400;
    yoe = (unsigned)(y - era * 400);
    d = m + ((m > 2) ? -3 : 9);
    doy = (unsigned)((153 * d + 2) / 5) + (unsigned)day - 1U;
    doe = yoe * 365U + yoe / 4U - yoe / 100U + doy;
    return (uint32_t)((long)era * 146097L + (long)doe - 719468L);
}

int hal_rtc_init(void)
{
    if (s_rtc_initialized) {
        return HAL_ALREADY_INIT;
    }
    memset(&s_rtc, 0, sizeof(s_rtc));
    s_rtc.Instance = RTC;
    if (HAL_RTC_Init(&s_rtc) != HAL_OK) {
        return HAL_ERROR;
    }
    s_rtc_initialized = true;
    return HAL_OK;
}

int hal_rtc_deinit(void)
{
    if (!s_rtc_initialized) {
        return HAL_NOT_READY;
    }
    (void)HAL_RTC_DeInit(&s_rtc);
    s_rtc_initialized = false;
    return HAL_OK;
}

int hal_rtc_set_time(const hal_rtc_time_t *time)
{
    RTC_TimeTypeDef hal_time;
    RTC_DateTypeDef hal_date;
    uint8_t weekday;

    if (time == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (!s_rtc_initialized) {
        return HAL_NOT_READY;
    }
    if (time->year < 2000U || time->month < 1U || time->month > 12U ||
        time->day < 1U || time->day > 31U || time->hour > 23U ||
        time->minute > 59U || time->second > 59U) {
        return HAL_INVALID_PARAM;
    }
    weekday = (time->weekday >= 1U && time->weekday <= 7U)
              ? time->weekday : (uint8_t)RTC_WEEKDAY_MONDAY;

    memset(&hal_time, 0, sizeof(hal_time));
    hal_time.Hours = time->hour;
    hal_time.Minutes = time->minute;
    hal_time.Seconds = time->second;
    hal_time.TimeFormat = RTC_HOURFORMAT_24;
    hal_time.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
    hal_time.StoreOperation = RTC_STOREOPERATION_RESET;

    memset(&hal_date, 0, sizeof(hal_date));
    hal_date.Year = (uint8_t)(time->year - 2000U);
    hal_date.Month = time->month;
    hal_date.Date = time->day;
    hal_date.WeekDay = weekday;

    if (HAL_RTC_SetTime(&s_rtc, &hal_time, RTC_FORMAT_BIN) != HAL_OK) {
        return HAL_ERROR;
    }
    if (HAL_RTC_SetDate(&s_rtc, &hal_date, RTC_FORMAT_BIN) != HAL_OK) {
        return HAL_ERROR;
    }
    return HAL_OK;
}

int hal_rtc_get_time(hal_rtc_time_t *time)
{
    RTC_TimeTypeDef hal_time;
    RTC_DateTypeDef hal_date;

    if (time == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (!s_rtc_initialized) {
        return HAL_NOT_READY;
    }
    memset(&hal_time, 0, sizeof(hal_time));
    memset(&hal_date, 0, sizeof(hal_date));
    if (HAL_RTC_GetTime(&s_rtc, &hal_time, RTC_FORMAT_BIN) != HAL_OK) {
        return HAL_ERROR;
    }
    if (HAL_RTC_GetDate(&s_rtc, &hal_date, RTC_FORMAT_BIN) != HAL_OK) {
        return HAL_ERROR;
    }
    time->year = (uint16_t)(2000U + hal_date.Year);
    time->month = (uint8_t)hal_date.Month;
    time->day = (uint8_t)hal_date.Date;
    time->hour = (uint8_t)hal_time.Hours;
    time->minute = (uint8_t)hal_time.Minutes;
    time->second = (uint8_t)hal_time.Seconds;
    time->weekday = (uint8_t)hal_date.WeekDay;
    return HAL_OK;
}

int hal_rtc_set_alarm(hal_rtc_alarm_e alarm,
                                 const hal_rtc_alarm_config_t *config)
{
    RTC_AlarmTypeDef hal_alarm;
    uint32_t alarm_id;

    if (config == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (!s_rtc_initialized) {
        return HAL_NOT_READY;
    }
    alarm_id = (alarm == HAL_RTC_ALARM_B) ? RTC_ALARM_B : RTC_ALARM_A;
    memset(&hal_alarm, 0, sizeof(hal_alarm));
    hal_alarm.AlarmTime.Hours = config->time.hour;
    hal_alarm.AlarmTime.Minutes = config->time.minute;
    hal_alarm.AlarmTime.Seconds = config->time.second;
    hal_alarm.AlarmTime.TimeFormat = RTC_HOURFORMAT_24;
    hal_alarm.AlarmTime.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
    hal_alarm.AlarmTime.StoreOperation = RTC_STOREOPERATION_RESET;
    hal_alarm.AlarmMask = RTC_ALARMMASK_NONE;
    hal_alarm.AlarmSubSecondMask = RTC_ALARMSUBSECONDMASK_ALL;
    hal_alarm.AlarmDateWeekDaySel = RTC_ALARMDATEWEEKDAYSEL_DATE;
    hal_alarm.AlarmDateWeekDay = 1U;
    hal_alarm.Alarm = alarm_id;

    s_rtc_alarm_callback = config->callback;
    s_rtc_alarm_user_data = config->user_data;
    return stm32_from_hal(HAL_RTC_SetAlarm_IT(&s_rtc, &hal_alarm, RTC_FORMAT_BIN));
}

int hal_rtc_disable_alarm(hal_rtc_alarm_e alarm)
{
    if (!s_rtc_initialized) {
        return HAL_NOT_READY;
    }
    return stm32_from_hal(HAL_RTC_DeactivateAlarm(&s_rtc,
                                               (alarm == HAL_RTC_ALARM_B) ? RTC_ALARM_B
                                                                           : RTC_ALARM_A));
}

int hal_rtc_get_timestamp(uint32_t *timestamp)
{
    hal_rtc_time_t time;
    int status;
    uint32_t days;

    if (timestamp == NULL) {
        return HAL_INVALID_PARAM;
    }
    status = hal_rtc_get_time(&time);
    if (status != HAL_OK) {
        return status;
    }
    days = stm32_epoch_days(time.year, time.month, time.day);
    *timestamp = days * 86400U + (uint32_t)time.hour * 3600U +
                 (uint32_t)time.minute * 60U + (uint32_t)time.second;
    return HAL_OK;
}

/* ---------------- FLASH ---------------- */
static uint32_t stm32_flash_bank(uintptr_t address)
{
    return ((address - STM32_FLASH_BASE_ADDR) >= STM32_FLASH_BANK_SIZE) ? FLASH_BANK_2
                                                                  : FLASH_BANK_1;
}

static uint32_t stm32_flash_sector(uintptr_t address)
{
    return (uint32_t)(((address - STM32_FLASH_BASE_ADDR) % STM32_FLASH_BANK_SIZE) /
                      STM32_FLASH_SECTOR_SIZE);
}

static bool stm32_flash_range_valid(uintptr_t address, size_t length)
{
    uintptr_t end;

    if (address < STM32_FLASH_BASE_ADDR || length == 0U) {
        return false;
    }
    if (length > (size_t)(STM32_FLASH_BASE_ADDR + STM32_FLASH_SIZE - address)) {
        return false;
    }
    end = address + length;
    return end <= (STM32_FLASH_BASE_ADDR + STM32_FLASH_SIZE);
}

int hal_flash_init(hal_flash_t *handle)
{
    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state != HAL_STATE_RESET) {
        return HAL_ALREADY_INIT;
    }
    memset(&handle->info, 0, sizeof(handle->info));
    handle->info.start = STM32_FLASH_BASE_ADDR;
    handle->info.size = STM32_FLASH_SIZE;
    handle->info.erase_size = STM32_FLASH_SECTOR_SIZE;
    handle->info.program_size = STM32_FLASH_WORD_SIZE;
    handle->info.flags = HAL_FLASH_FLAG_READ | HAL_FLASH_FLAG_WRITE |
                         HAL_FLASH_FLAG_ERASE;
    return stm32_base_init(&handle->base, NULL);
}

int hal_flash_deinit(hal_flash_t *handle)
{
    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    stm32_base_reset(&handle->base);
    return HAL_OK;
}

int hal_flash_get_info(hal_flash_t *handle, hal_flash_info_t *info)
{
    if (handle == NULL || info == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    *info = handle->info;
    return HAL_OK;
}

int hal_flash_read(hal_flash_t *handle, uintptr_t address,
                              void *buffer, size_t length)
{
    if (handle == NULL || buffer == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    if (!stm32_flash_range_valid(address, length)) {
        return HAL_INVALID_PARAM;
    }
    memcpy(buffer, (const void *)address, length);
    return HAL_OK;
}

int hal_flash_write(hal_flash_t *handle, uintptr_t address,
                               const void *buffer, size_t length)
{
    static uint32_t word[STM32_FLASH_WORD_SIZE / 4U];
    const uint8_t *source = (const uint8_t *)buffer;
    size_t offset;

    if (handle == NULL || buffer == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    if (!stm32_flash_range_valid(address, length) ||
        (address % STM32_FLASH_WORD_SIZE) != 0U) {
        return HAL_INVALID_PARAM;
    }
    if (HAL_FLASH_Unlock() != HAL_OK) {
        return HAL_ERROR;
    }
    for (offset = 0U; offset < length; offset += STM32_FLASH_WORD_SIZE) {
        size_t chunk = HAL_MIN((size_t)STM32_FLASH_WORD_SIZE, length - offset);
        memset(word, 0xFF, sizeof(word));
        memcpy(word, source + offset, chunk);
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_FLASHWORD,
                              (uint32_t)(address + offset),
                              (uint32_t)(uintptr_t)word) != HAL_OK) {
            (void)HAL_FLASH_Lock();
            return HAL_ERROR;
        }
    }
    (void)HAL_FLASH_Lock();
    return HAL_OK;
}

int hal_flash_erase(hal_flash_t *handle, uintptr_t address,
                               size_t length)
{
    FLASH_EraseInitTypeDef erase;
    uint32_t sector_error = 0U;
    uintptr_t current;
    size_t remaining;

    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    if (!stm32_flash_range_valid(address, length)) {
        return HAL_INVALID_PARAM;
    }
    if (HAL_FLASH_Unlock() != HAL_OK) {
        return HAL_ERROR;
    }
    current = address;
    remaining = length;
    while (remaining > 0U) {
        size_t sector_offset = (size_t)((current - STM32_FLASH_BASE_ADDR) %
                                        STM32_FLASH_SECTOR_SIZE);
        size_t chunk = STM32_FLASH_SECTOR_SIZE - sector_offset;

        if (chunk > remaining) {
            chunk = remaining;
        }
        memset(&erase, 0, sizeof(erase));
        erase.TypeErase = FLASH_TYPEERASE_SECTORS;
        erase.Banks = stm32_flash_bank(current);
        erase.Sector = stm32_flash_sector(current);
        erase.NbSectors = 1U;
        erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;
        if (HAL_FLASHEx_Erase(&erase, &sector_error) != HAL_OK) {
            (void)HAL_FLASH_Lock();
            return HAL_ERROR;
        }
        current += chunk;
        remaining -= chunk;
    }
    (void)HAL_FLASH_Lock();
    return HAL_OK;
}

int hal_flash_lock(hal_flash_t *handle)
{
    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    return stm32_from_hal(HAL_FLASH_Lock());
}

int hal_flash_unlock(hal_flash_t *handle)
{
    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    return stm32_from_hal(HAL_FLASH_Unlock());
}

/* ---------------- CRC ---------------- */
static uint32_t stm32_crc_reflect(uint32_t value, uint8_t bits)
{
    uint32_t result = 0U;
    uint8_t i;

    for (i = 0U; i < bits; i++) {
        if ((value & 1UL) != 0UL) {
            result |= (1UL << (bits - 1U - i));
        }
        value >>= 1;
    }
    return result;
}

static uint32_t stm32_crc_software(const hal_crc_config_t *config, const uint8_t *data,
                                size_t length)
{
    uint32_t mask = (config->width == 32U) ? 0xFFFFFFFFU
                                           : ((1UL << config->width) - 1UL);
    uint32_t top = 1UL << ((uint32_t)config->width - 1U);
    uint32_t crc = config->initial_value & mask;
    size_t i;

    for (i = 0U; i < length; i++) {
        uint8_t byte = data[i];
        uint8_t bit;

        if (config->input_reverse) {
            byte = (uint8_t)stm32_crc_reflect(byte, 8U);
        }
        crc ^= ((uint32_t)byte << ((uint32_t)config->width - 8U));
        for (bit = 0U; bit < 8U; bit++) {
            if ((crc & top) != 0UL) {
                crc = (crc << 1) ^ config->polynomial;
            } else {
                crc <<= 1;
            }
        }
        crc &= mask;
    }
    if (config->output_reverse) {
        crc = stm32_crc_reflect(crc, (uint8_t)config->width);
    }
    return (crc ^ config->xor_output) & mask;
}

int hal_crc_init(hal_crc_t *handle, const hal_crc_config_t *config)
{
    if (handle == NULL || config == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (config->width != HAL_CRC_WIDTH_8 && config->width != HAL_CRC_WIDTH_16 &&
        config->width != HAL_CRC_WIDTH_32) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state != HAL_STATE_RESET) {
        return HAL_ALREADY_INIT;
    }
    handle->config = *config;
    s_crc_initialized = true;
    /* H7 CRC 外设多项式固定为 0x04C11DB7，无法覆盖可配置的 width/poly/reflect，
     * 因此统一使用软件实现以保证参数语义一致。 */
    return stm32_base_init(&handle->base, &s_crc);
}

int hal_crc_reset(hal_crc_t *handle)
{
    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    return HAL_OK;
}

int hal_crc_calculate(hal_crc_t *handle, const void *data,
                                 size_t length, uint32_t *result)
{
    if (handle == NULL || data == NULL || result == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    *result = stm32_crc_software(&handle->config, (const uint8_t *)data, length);
    return HAL_OK;
}

int hal_crc_deinit(hal_crc_t *handle)
{
    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    stm32_base_reset(&handle->base);
    return HAL_OK;
}

/* ---------------- RNG ---------------- */
int hal_rng_init(hal_rng_t *handle)
{
    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state != HAL_STATE_RESET) {
        return HAL_ALREADY_INIT;
    }
    if (!s_rng_initialized) {
        memset(&s_rng, 0, sizeof(s_rng));
        s_rng.Instance = RNG;
        s_rng.Init.ClockErrorDetection = RNG_CED_ENABLE;
        if (HAL_RNG_Init(&s_rng) != HAL_OK) {
            return HAL_ERROR;
        }
        s_rng_initialized = true;
    }
    return stm32_base_init(&handle->base, &s_rng);
}

int hal_rng_deinit(hal_rng_t *handle)
{
    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    stm32_base_reset(&handle->base);
    return HAL_OK;
}

int hal_rng_get(hal_rng_t *handle, uint32_t *value)
{
    if (handle == NULL || value == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    if (!s_rng_initialized) {
        return HAL_NOT_READY;
    }
    return stm32_from_hal(HAL_RNG_GenerateRandomNumber(&s_rng, value));
}

int hal_rng_generate(hal_rng_t *handle, void *buffer, size_t length)
{
    uint8_t *output = (uint8_t *)buffer;
    size_t offset = 0U;

    if (handle == NULL || buffer == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    if (!s_rng_initialized) {
        return HAL_NOT_READY;
    }
    while (offset < length) {
        uint32_t value = 0U;
        size_t chunk = HAL_MIN(sizeof(value), length - offset);
        HAL_StatusTypeDef status = HAL_RNG_GenerateRandomNumber(&s_rng, &value);
        if (status != HAL_OK) {
            return stm32_from_hal(status);
        }
        memcpy(output + offset, &value, chunk);
        offset += chunk;
    }
    return HAL_OK;
}

/* ---------------- PWR ---------------- */
int hal_pwr_init(void)
{
    /* 低功耗相关时钟在 RCC 中默认开启，无需额外配置。 */
    return HAL_OK;
}

int hal_pwr_enter(const hal_pwr_config_t *config)
{
    if (config == NULL) {
        return HAL_INVALID_PARAM;
    }
    switch (config->mode) {
    case HAL_PWR_MODE_SLEEP:
        HAL_PWR_EnterSLEEPMode(PWR_MAINREGULATOR_ON, PWR_SLEEPENTRY_WFI);
        break;
    case HAL_PWR_MODE_STOP:
        HAL_PWR_EnterSTOPMode(config->low_power_regulator ? PWR_LOWPOWERREGULATOR_ON
                                                           : PWR_MAINREGULATOR_ON,
                              PWR_STOPENTRY_WFI);
        break;
    case HAL_PWR_MODE_STANDBY:
        HAL_PWR_EnterSTANDBYMode();
        break;
    default:
        return HAL_INVALID_PARAM;
    }
    return HAL_OK;
}

int hal_pwr_get_status(hal_pwr_status_t *status)
{
    if (status == NULL) {
        return HAL_INVALID_PARAM;
    }
    memset(status, 0, sizeof(*status));
    status->reset_flags = RCC->RSR;
    /* 唤醒标志位因内核/电源域差异较大，未在此处解析。 */
    status->wakeup_flags = 0U;
    return HAL_OK;
}

int hal_pwr_clear_flags(void)
{
    __HAL_RCC_CLEAR_RESET_FLAGS();
    return HAL_OK;
}

/* ---------------- QSPI ---------------- */
static uint32_t stm32_qspi_prescaler(uint32_t clock_frequency)
{
    uint32_t hclk = (uint32_t)HAL_RCC_GetHCLKFreq();
    uint32_t div;

    if (clock_frequency == 0U) {
        return 0U;
    }
    div = (hclk + clock_frequency - 1U) / clock_frequency;
    if (div == 0U) {
        div = 1U;
    }
    div -= 1U;
    return (div > 255U) ? 255U : div;
}

static uint32_t stm32_qspi_address_size(uint8_t size)
{
    switch (size) {
    case 8U:  return QSPI_ADDRESS_8_BITS;
    case 16U: return QSPI_ADDRESS_16_BITS;
    case 32U: return QSPI_ADDRESS_32_BITS;
    default:  return QSPI_ADDRESS_24_BITS;
    }
}

static uint32_t stm32_qspi_lines(uint8_t lines)
{
    switch (lines) {
    case 2U: return 2U;
    case 4U: return 4U;
    default: return 1U;
    }
}

int hal_qspi_init(hal_qspi_t *handle, const hal_qspi_config_t *config)
{
    if (handle == NULL || config == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state != HAL_STATE_RESET) {
        return HAL_ALREADY_INIT;
    }
    if (s_qspi_initialized) {
        return HAL_ALREADY_INIT;
    }
    memset(&s_qspi, 0, sizeof(s_qspi));
    s_qspi.Instance = QUADSPI;
    s_qspi.Init.ClockPrescaler = stm32_qspi_prescaler(config->clock_frequency);
    s_qspi.Init.FifoThreshold = 4U;
    s_qspi.Init.SampleShifting = QSPI_SAMPLE_SHIFTING_NONE;
    s_qspi.Init.FlashSize = config->flash_size;
    s_qspi.Init.ChipSelectHighTime = QSPI_CS_HIGH_TIME_1_CYCLE;
    s_qspi.Init.ClockMode = (config->clock_mode == 0U) ? QSPI_CLOCK_MODE_0
                                                       : QSPI_CLOCK_MODE_3;
    s_qspi.Init.FlashID = QSPI_FLASH_ID_1;
    s_qspi.Init.DualFlash = config->dual_flash ? QSPI_DUALFLASH_ENABLE
                                               : QSPI_DUALFLASH_DISABLE;
    if (HAL_QSPI_Init(&s_qspi) != HAL_OK) {
        return HAL_ERROR;
    }
    s_qspi_initialized = true;
    handle->config = *config;
    handle->dma = NULL;
    handle->callback = NULL;
    handle->user_data = NULL;
    return stm32_base_init(&handle->base, &s_qspi);
}

int hal_qspi_deinit(hal_qspi_t *handle)
{
    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    (void)HAL_QSPI_DeInit(&s_qspi);
    s_qspi_initialized = false;
    stm32_base_reset(&handle->base);
    return HAL_OK;
}

int hal_qspi_command(hal_qspi_t *handle,
                                const hal_qspi_command_t *command)
{
    QSPI_CommandTypeDef hal_command;
    uint32_t lines;

    if (handle == NULL || command == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    memset(&hal_command, 0, sizeof(hal_command));
    hal_command.Instruction = command->instruction;
    hal_command.InstructionMode = QSPI_INSTRUCTION_1_LINE;
    hal_command.Address = command->address;
    hal_command.AddressSize = stm32_qspi_address_size(command->address_size);
    lines = stm32_qspi_lines(command->address_lines);
    hal_command.AddressMode = (command->address_lines == 0U) ? QSPI_ADDRESS_NONE
                              : ((lines == 2U) ? QSPI_ADDRESS_2_LINES
                                               : ((lines == 4U) ? QSPI_ADDRESS_4_LINES
                                                                : QSPI_ADDRESS_1_LINE));
    lines = stm32_qspi_lines(command->data_lines);
    hal_command.DataMode = (command->data_lines == 0U) ? QSPI_DATA_NONE
                           : ((lines == 2U) ? QSPI_DATA_2_LINES
                                            : ((lines == 4U) ? QSPI_DATA_4_LINES
                                                             : QSPI_DATA_1_LINE));
    hal_command.NbData = (uint32_t)command->data_length;
    hal_command.DummyCycles = command->dummy_cycles;
    hal_command.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    hal_command.AlternateBytes = 0U;
    hal_command.AlternateBytesSize = QSPI_ALTERNATE_BYTES_8_BITS;
    hal_command.DdrMode = QSPI_DDR_MODE_DISABLE;
    hal_command.DdrHoldHalfCycle = QSPI_DDR_HHC_ANALOG_DELAY;
    hal_command.SIOOMode = QSPI_SIOO_INST_EVERY_CMD;
    return stm32_from_hal(HAL_QSPI_Command(&s_qspi, &hal_command,
                                        HAL_QSPI_TIMEOUT_DEFAULT_VALUE));
}

int hal_qspi_transmit(hal_qspi_t *handle, const void *data,
                                 size_t length, uint32_t timeout)
{
    if (handle == NULL || data == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    if (length == 0U) {
        return HAL_INVALID_PARAM;
    }
    return stm32_from_hal(HAL_QSPI_Transmit(&s_qspi, (uint8_t *)data,
                                         stm32_timeout(timeout)));
}

int hal_qspi_receive(hal_qspi_t *handle, void *data,
                                size_t length, uint32_t timeout)
{
    if (handle == NULL || data == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    if (length == 0U) {
        return HAL_INVALID_PARAM;
    }
    return stm32_from_hal(HAL_QSPI_Receive(&s_qspi, data, stm32_timeout(timeout)));
}

int hal_qspi_memory_map(hal_qspi_t *handle)
{
    QSPI_MemoryMappedTypeDef memory_mapped;

    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    memset(&memory_mapped, 0, sizeof(memory_mapped));
    memory_mapped.TimeOutCfg.TimeOutPeriod = 0U;
    memory_mapped.TimeOutCfg.TimeOutActivation = QSPI_TIMEOUT_COUNTER_DISABLE;
    return stm32_from_hal(HAL_QSPI_MemoryMapped(&s_qspi, &memory_mapped));
}

int hal_qspi_memory_unmap(hal_qspi_t *handle)
{
    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    return stm32_from_hal(HAL_QSPI_Abort(&s_qspi));
}

/* ---------------- SDIO / SDMMC ---------------- */
static uint32_t stm32_sd_bus_width(hal_sdio_bus_width_e width)
{
    switch (width) {
    case HAL_SDIO_BUS_4BIT: return SDMMC_BUS_WIDE_4B;
    case HAL_SDIO_BUS_8BIT: return SDMMC_BUS_WIDE_8B;
    default:                 return SDMMC_BUS_WIDE_1B;
    }
}

int hal_sdio_init(hal_sdio_t *handle, const hal_sdio_config_t *config)
{
    uint32_t sdmmc_clock;
    uint32_t clock_div;

    if (handle == NULL || config == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state != HAL_STATE_RESET) {
        return HAL_ALREADY_INIT;
    }
    if (s_sd_initialized) {
        return HAL_ALREADY_INIT;
    }
    memset(&s_sd, 0, sizeof(s_sd));
    s_sd.Instance = SDMMC1;
    s_sd.Init.ClockEdge = SDMMC_CLOCK_EDGE_RISING;
    s_sd.Init.ClockBypass = SDMMC_CLOCK_BYPASS_DISABLE;
    s_sd.Init.ClockPowerSave = SDMMC_CLOCK_POWER_SAVE_DISABLE;
    s_sd.Init.BusWide = stm32_sd_bus_width(config->bus_width);
    s_sd.Init.HardwareFlowControl = SDMMC_HARDWARE_FLOW_CONTROL_DISABLE;
    sdmmc_clock = (uint32_t)HAL_RCC_GetHCLKFreq() / 2U;
    clock_div = (config->clock_frequency == 0U)
                ? 1U : (sdmmc_clock / (2U * config->clock_frequency));
    if (clock_div == 0U) {
        clock_div = 1U;
    }
    if (clock_div > 1023U) {
        clock_div = 1023U;
    }
    s_sd.Init.ClockDiv = clock_div;
    if (HAL_SD_Init(&s_sd) != HAL_OK) {
        return HAL_ERROR;
    }
    s_sd_initialized = true;
    handle->config = *config;
    handle->dma = NULL;
    handle->callback = NULL;
    handle->user_data = NULL;
    return stm32_base_init(&handle->base, &s_sd);
}

int hal_sdio_deinit(hal_sdio_t *handle)
{
    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    (void)HAL_SD_DeInit(&s_sd);
    s_sd_initialized = false;
    stm32_base_reset(&handle->base);
    return HAL_OK;
}

int hal_sdio_read(hal_sdio_t *handle, uint32_t block,
                             void *buffer, size_t block_count, uint32_t timeout)
{
    if (handle == NULL || buffer == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    if (block_count == 0U) {
        return HAL_INVALID_PARAM;
    }
    return stm32_from_hal(HAL_SD_ReadBlocks(&s_sd, (uint8_t *)buffer, block,
                                         (uint32_t)block_count, stm32_timeout(timeout)));
}

int hal_sdio_write(hal_sdio_t *handle, uint32_t block,
                              const void *buffer, size_t block_count, uint32_t timeout)
{
    if (handle == NULL || buffer == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    if (block_count == 0U) {
        return HAL_INVALID_PARAM;
    }
    return stm32_from_hal(HAL_SD_WriteBlocks(&s_sd, (uint8_t *)buffer, block,
                                          (uint32_t)block_count, stm32_timeout(timeout)));
}

int hal_sdio_read_dma(hal_sdio_t *handle, uint32_t block,
                                 void *buffer, size_t block_count)
{
    if (handle == NULL || buffer == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    if (block_count == 0U) {
        return HAL_INVALID_PARAM;
    }
    return stm32_from_hal(HAL_SD_ReadBlocks_DMA(&s_sd, (uint8_t *)buffer, block,
                                             (uint32_t)block_count));
}

int hal_sdio_write_dma(hal_sdio_t *handle, uint32_t block,
                                  const void *buffer, size_t block_count)
{
    if (handle == NULL || buffer == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    if (block_count == 0U) {
        return HAL_INVALID_PARAM;
    }
    return stm32_from_hal(HAL_SD_WriteBlocks_DMA(&s_sd, (uint8_t *)buffer, block,
                                              (uint32_t)block_count));
}

/* ---------------- USB ---------------- */
/*
 * H7 的 USB 需要 HAL_PCD（设备）或 HAL_HCD（主机）以及 CubeMX 生成的
 * USB 描述符与中断处理。本平台层只维护连接/启停状态，不做实际枚举；
 * 应用应在 MX_USB_DEVICE_Init / MX_USB_HOST_Init 中完成 HAL 初始化，
 * 并通过 HAL_PCD_Start/HAL_PCD_Stop 等接口接管底层。
 */
int hal_usb_init(hal_usb_t *handle, const hal_usb_config_t *config)
{
    if (handle == NULL || config == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state != HAL_STATE_RESET) {
        return HAL_ALREADY_INIT;
    }
    if (config->mode != HAL_USB_MODE_DEVICE && config->mode != HAL_USB_MODE_HOST) {
        return HAL_INVALID_PARAM;
    }
    handle->config = *config;
    handle->callback = NULL;
    handle->user_data = NULL;
    s_usb_initialized = true;
    s_usb_connected = false;
    return stm32_base_init(&handle->base, NULL);
}

int hal_usb_deinit(hal_usb_t *handle)
{
    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    s_usb_initialized = false;
    s_usb_connected = false;
    stm32_base_reset(&handle->base);
    return HAL_OK;
}

int hal_usb_start(hal_usb_t *handle)
{
    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    if (!s_usb_initialized) {
        return HAL_NOT_READY;
    }
    handle->base.state = HAL_STATE_READY;
    return HAL_OK;
}

int hal_usb_stop(hal_usb_t *handle)
{
    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    handle->base.state = HAL_STATE_SUSPENDED;
    return HAL_OK;
}

int hal_usb_connect(hal_usb_t *handle)
{
    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    if (!s_usb_initialized) {
        return HAL_NOT_READY;
    }
    s_usb_connected = true;
    return HAL_OK;
}

int hal_usb_disconnect(hal_usb_t *handle)
{
    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    s_usb_connected = false;
    return HAL_OK;
}

/* ---------------- ETH ---------------- */
int hal_eth_init(hal_eth_t *handle, const hal_eth_config_t *config)
{
    if (handle == NULL || config == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state != HAL_STATE_RESET) {
        return HAL_ALREADY_INIT;
    }
    if (s_eth_initialized) {
        return HAL_ALREADY_INIT;
    }
    memset(&s_eth, 0, sizeof(s_eth));
    s_eth.Instance = ETH;
    s_eth.Init.AutoNegotiation = ETH_AUTONEGOTIATION_DISABLE;
    s_eth.Init.Speed = (config->speed == 100U) ? ETH_SPEED_100M : ETH_SPEED_10M;
    s_eth.Init.DuplexMode = config->full_duplex ? ETH_MODE_FULLDUPLEX
                                                : ETH_MODE_HALFDUPLEX;
    s_eth.Init.MediaInterface = ETH_MEDIA_INTERFACE_RMII;
    s_eth.Init.PhyAddress = 0U;
    s_eth.Init.RxMode = ETH_RXPOLLING_MODE;
    s_eth.Init.TxMode = ETH_TXPOLLING_MODE;
    s_eth.Init.ChecksumMode = config->checksum_offload ? ETH_CHECKSUM_BY_HARDWARE
                                                        : ETH_CHECKSUM_BY_SOFTWARE;
    if (HAL_ETH_Init(&s_eth) != HAL_OK) {
        /* 需要 CubeMX 提供的 DMA 描述符/缓冲区；缺失时视为不支持。 */
        return HAL_NOT_SUPPORTED;
    }
    s_eth_initialized = true;
    handle->config = *config;
    handle->rx_dma = NULL;
    handle->tx_dma = NULL;
    handle->callback = NULL;
    handle->user_data = NULL;
    return stm32_base_init(&handle->base, &s_eth);
}

int hal_eth_deinit(hal_eth_t *handle)
{
    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    (void)HAL_ETH_Stop(&s_eth);
    (void)HAL_ETH_DeInit(&s_eth);
    s_eth_initialized = false;
    stm32_base_reset(&handle->base);
    return HAL_OK;
}

int hal_eth_start(hal_eth_t *handle)
{
    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    return stm32_from_hal(HAL_ETH_Start(&s_eth));
}

int hal_eth_stop(hal_eth_t *handle)
{
    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    return stm32_from_hal(HAL_ETH_Stop(&s_eth));
}

int hal_eth_transmit(hal_eth_t *handle, const void *data,
                                size_t length)
{
    if (handle == NULL || data == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    if (s_eth.TxDesc == NULL || s_eth.TxDesc->Buffer1Addr == 0U) {
        return HAL_NOT_SUPPORTED;
    }
    memcpy((void *)(uintptr_t)s_eth.TxDesc->Buffer1Addr, data, length);
    return stm32_from_hal(HAL_ETH_TransmitFrame(&s_eth, (uint32_t)length));
}

int hal_eth_receive(hal_eth_t *handle, void *buffer,
                               size_t buffer_size, size_t *received_length)
{
    void *data = NULL;
    uint32_t length = 0U;

    if (handle == NULL || buffer == NULL || received_length == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_base_check(&handle->base) != HAL_OK) {
        return HAL_NOT_READY;
    }
    if (HAL_ETH_ReadData(&s_eth, &data) != HAL_OK) {
        return HAL_TIMEOUT;
    }
    if (HAL_ETH_GetRxDataLength(&s_eth, &length) != HAL_OK) {
        (void)HAL_ETH_BuildRxDescriptors(&s_eth);
        return HAL_ERROR;
    }
    if (length > buffer_size) {
        (void)HAL_ETH_BuildRxDescriptors(&s_eth);
        return HAL_OVERFLOW;
    }
    memcpy(buffer, data, length);
    *received_length = (size_t)length;
    (void)HAL_ETH_BuildRxDescriptors(&s_eth);
    return HAL_OK;
}

/* ---------------- HAL 回调分发 ---------------- */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    stm32_exti_dispatch(GPIO_Pin);
}

static void stm32_uart_dispatch(UART_HandleTypeDef *huart, hal_uart_event_e event,
                             size_t length)
{
    stm32_slot_t *slot = stm32_slot_find(STM32_SLOT_UART, huart);
    hal_uart_t *handle;

    if (slot == NULL || slot->owner == NULL) {
        return;
    }
    handle = (hal_uart_t *)slot->owner;
    if (event == HAL_UART_EVENT_RX_COMPLETE || event == HAL_UART_EVENT_RX_HALF ||
        event == HAL_UART_EVENT_RX_IDLE) {
        slot->rx_length = length;
    }
    if (event == HAL_UART_EVENT_ERROR) {
        handle->base.error = HAL_IO_ERROR;
        handle->base.state = HAL_STATE_ERROR;
    } else {
        handle->base.state = HAL_STATE_READY;
    }
    if (handle->callback != NULL) {
        handle->callback(handle, event, length, handle->user_data);
    }
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    stm32_uart_dispatch(huart, HAL_UART_EVENT_TX_COMPLETE, 0U);
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    stm32_uart_dispatch(huart, HAL_UART_EVENT_RX_COMPLETE, 0U);
}

void HAL_UART_RxHalfCpltCallback(UART_HandleTypeDef *huart)
{
    stm32_uart_dispatch(huart, HAL_UART_EVENT_RX_HALF, 0U);
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    stm32_uart_dispatch(huart, HAL_UART_EVENT_ERROR, 0U);
}

void HAL_UART_AbortCpltCallback(UART_HandleTypeDef *huart)
{
    stm32_uart_dispatch(huart, HAL_UART_EVENT_ABORT, 0U);
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    stm32_uart_dispatch(huart, HAL_UART_EVENT_RX_IDLE, (size_t)Size);
}

void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi)
{
    stm32_slot_t *slot = stm32_slot_find(STM32_SLOT_SPI, hspi);
    hal_spi_t *handle;

    if (slot != NULL && slot->owner != NULL) {
        handle = (hal_spi_t *)slot->owner;
        handle->base.state = HAL_STATE_READY;
        if (handle->callback != NULL) {
            handle->callback(handle, HAL_EVENT_TX_COMPLETE, handle->user_data);
        }
    }
}

void HAL_SPI_RxCpltCallback(SPI_HandleTypeDef *hspi)
{
    stm32_slot_t *slot = stm32_slot_find(STM32_SLOT_SPI, hspi);
    hal_spi_t *handle;

    if (slot != NULL && slot->owner != NULL) {
        handle = (hal_spi_t *)slot->owner;
        handle->base.state = HAL_STATE_READY;
        if (handle->callback != NULL) {
            handle->callback(handle, HAL_EVENT_RX_COMPLETE, handle->user_data);
        }
    }
}

void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *hspi)
{
    stm32_slot_t *slot = stm32_slot_find(STM32_SLOT_SPI, hspi);
    hal_spi_t *handle;

    if (slot != NULL && slot->owner != NULL) {
        handle = (hal_spi_t *)slot->owner;
        handle->base.state = HAL_STATE_READY;
        if (handle->callback != NULL) {
            handle->callback(handle, HAL_EVENT_COMPLETE, handle->user_data);
        }
    }
}

void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *hspi)
{
    stm32_slot_t *slot = stm32_slot_find(STM32_SLOT_SPI, hspi);

    if (slot != NULL && slot->owner != NULL) {
        hal_spi_t *handle = (hal_spi_t *)slot->owner;
        handle->base.error = HAL_IO_ERROR;
        handle->base.state = HAL_STATE_ERROR;
        if (handle->callback != NULL) {
            handle->callback(handle, HAL_EVENT_ERROR, handle->user_data);
        }
    }
}

void HAL_I2C_MasterTxCpltCallback(I2C_HandleTypeDef *hi2c)
{
    stm32_slot_t *slot = stm32_slot_find(STM32_SLOT_I2C, hi2c);

    if (slot != NULL && slot->owner != NULL) {
        hal_i2c_t *handle = (hal_i2c_t *)slot->owner;
        if (handle->callback != NULL) {
            handle->callback(handle, HAL_EVENT_TX_COMPLETE, handle->user_data);
        }
    }
}

void HAL_I2C_MasterRxCpltCallback(I2C_HandleTypeDef *hi2c)
{
    stm32_slot_t *slot = stm32_slot_find(STM32_SLOT_I2C, hi2c);

    if (slot != NULL && slot->owner != NULL) {
        hal_i2c_t *handle = (hal_i2c_t *)slot->owner;
        if (handle->callback != NULL) {
            handle->callback(handle, HAL_EVENT_RX_COMPLETE, handle->user_data);
        }
    }
}

void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *hi2c)
{
    stm32_slot_t *slot = stm32_slot_find(STM32_SLOT_I2C, hi2c);

    if (slot != NULL && slot->owner != NULL) {
        hal_i2c_t *handle = (hal_i2c_t *)slot->owner;
        handle->base.error = HAL_IO_ERROR;
        if (handle->callback != NULL) {
            handle->callback(handle, HAL_EVENT_ERROR, handle->user_data);
        }
    }
}

void HAL_I2S_TxCpltCallback(I2S_HandleTypeDef *hi2s)
{
    stm32_slot_t *slot = stm32_slot_find(STM32_SLOT_I2S, hi2s);

    if (slot != NULL && slot->owner != NULL) {
        hal_i2s_t *handle = (hal_i2s_t *)slot->owner;
        handle->base.state = HAL_STATE_READY;
        if (handle->callback != NULL) {
            handle->callback(handle, HAL_EVENT_TX_COMPLETE, handle->user_data);
        }
    }
}

void HAL_I2S_RxCpltCallback(I2S_HandleTypeDef *hi2s)
{
    stm32_slot_t *slot = stm32_slot_find(STM32_SLOT_I2S, hi2s);

    if (slot != NULL && slot->owner != NULL) {
        hal_i2s_t *handle = (hal_i2s_t *)slot->owner;
        handle->base.state = HAL_STATE_READY;
        if (handle->callback != NULL) {
            handle->callback(handle, HAL_EVENT_RX_COMPLETE, handle->user_data);
        }
    }
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    stm32_slot_t *slot = stm32_slot_find(STM32_SLOT_TIM, htim);

    if (slot != NULL && slot->owner != NULL) {
        hal_tim_t *handle = (hal_tim_t *)slot->owner;
        if (handle->callback != NULL) {
            handle->callback(handle, HAL_EVENT_COMPLETE, handle->user_data);
        }
    }
}

void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim)
{
    stm32_slot_t *slot = stm32_slot_find(STM32_SLOT_CAPTURE, htim);

    if (slot != NULL && slot->owner != NULL) {
        hal_capture_t *handle = (hal_capture_t *)slot->owner;
        if (handle->callback != NULL) {
            handle->callback(handle, HAL_EVENT_COMPLETE, handle->user_data);
        }
    }
}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
    stm32_slot_t *slot = stm32_slot_find(STM32_SLOT_ADC, hadc);

    if (slot != NULL && slot->owner != NULL) {
        hal_adc_t *handle = (hal_adc_t *)slot->owner;
        if (handle->callback != NULL) {
            handle->callback(handle, HAL_EVENT_COMPLETE, handle->user_data);
        }
    }
}

void HAL_ADC_ErrorCallback(ADC_HandleTypeDef *hadc)
{
    stm32_slot_t *slot = stm32_slot_find(STM32_SLOT_ADC, hadc);

    if (slot != NULL && slot->owner != NULL) {
        hal_adc_t *handle = (hal_adc_t *)slot->owner;
        handle->base.error = HAL_IO_ERROR;
        if (handle->callback != NULL) {
            handle->callback(handle, HAL_EVENT_ERROR, handle->user_data);
        }
    }
}

void HAL_FDCAN_TxFifoEmptyCallback(FDCAN_HandleTypeDef *hfdcan)
{
    stm32_slot_t *slot = stm32_slot_find(STM32_SLOT_CAN, hfdcan);

    if (slot != NULL && slot->owner != NULL) {
        hal_can_t *handle = (hal_can_t *)slot->owner;
        if (handle->callback != NULL) {
            handle->callback(handle, HAL_EVENT_TX_COMPLETE, handle->user_data);
        }
    }
}

void HAL_FDCAN_RxFifo0Callback(FDCAN_HandleTypeDef *hfdcan, uint32_t RxFifo0ITs)
{
    stm32_slot_t *slot = stm32_slot_find(STM32_SLOT_CAN, hfdcan);

    HAL_UNUSED(RxFifo0ITs);
    if (slot != NULL && slot->owner != NULL) {
        hal_can_t *handle = (hal_can_t *)slot->owner;
        if (handle->callback != NULL) {
            handle->callback(handle, HAL_EVENT_RX_COMPLETE, handle->user_data);
        }
    }
}

void HAL_RTC_AlarmAEventCallback(RTC_HandleTypeDef *hrtc)
{
    HAL_UNUSED(hrtc);
    if (s_rtc_alarm_callback != NULL) {
        s_rtc_alarm_callback(HAL_RTC_ALARM_A, s_rtc_alarm_user_data);
    }
}

void HAL_RTC_AlarmBEventCallback(RTC_HandleTypeDef *hrtc)
{
    HAL_UNUSED(hrtc);
    if (s_rtc_alarm_callback != NULL) {
        s_rtc_alarm_callback(HAL_RTC_ALARM_B, s_rtc_alarm_user_data);
    }
}

/* ---------------- 能力查询 ---------------- */
uint32_t hal_get_capabilities(hal_device_type_e type, uint8_t instance)
{
    HAL_UNUSED(instance);
    switch (type) {
    case HAL_DEVICE_UART:
        return HAL_CAP_DMA | HAL_CAP_IT | HAL_CAP_IDLE_DETECT |
               HAL_CAP_HALF_TRANSFER | HAL_CAP_CIRCULAR;
    case HAL_DEVICE_SPI:
        return HAL_CAP_DMA | HAL_CAP_IT | HAL_CAP_CIRCULAR | HAL_CAP_FIFO;
    case HAL_DEVICE_I2C:
        return HAL_CAP_DMA | HAL_CAP_IT | HAL_CAP_CIRCULAR;
    case HAL_DEVICE_CAN:
        return HAL_CAP_IT | HAL_CAP_FIFO;
    case HAL_DEVICE_ADC:
        return HAL_CAP_DMA | HAL_CAP_IT | HAL_CAP_CIRCULAR;
    case HAL_DEVICE_DAC:
        return HAL_CAP_DMA | HAL_CAP_IT | HAL_CAP_CIRCULAR;
    case HAL_DEVICE_PWM:
        return HAL_CAP_IT;
    case HAL_DEVICE_TIMER:
        return HAL_CAP_IT;
    case HAL_DEVICE_FLASH:
        return HAL_FLASH_FLAG_READ | HAL_FLASH_FLAG_WRITE | HAL_FLASH_FLAG_ERASE;
    case HAL_DEVICE_ETH:
        return HAL_CAP_DMA | HAL_CAP_IT;
    case HAL_DEVICE_GPIO:
    case HAL_DEVICE_USB:
    default:
        return 0U;
    }
}

#endif /* IOT_STM32_H7 */
