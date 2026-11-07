/*
 * AVR 平台实现（新 PORT 硬件抽象层），寄存器级，不依赖 HAL。
 *
 * 目标：ATmega328P @ 16 MHz（Arduino UNO 形态）。
 * GPIO 编码约定：PORT_PIN(port, pin) 中 port 0 -> PORTB，1 -> PORTC，
 * 2 -> PORTD；pin 为 0..7。逻辑端口号沿用了 STM32 风格的连续编号。
 *
 * 中断向量：本文件定义了 Timer0 溢出（毫秒时基）、Timer0/1/2 比较、
 * Timer1 输入捕获与 EXTI 分发所需的 ISR。若应用需要自定义这些向量，
 * 应关闭对应外设或修改本文件。
 */
#include "port.h"

#ifndef F_CPU
#define F_CPU 16000000UL
#endif

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <avr/sleep.h>
#include <avr/wdt.h>
#include <util/delay.h>

#include <string.h>

/* ---------------- 常量 ---------------- */
#define AVR_GPIO_PORT_COUNT     3U
#define AVR_FLASH_SIZE          32768UL
#define AVR_FLASH_PAGE_SIZE     128U
#define AVR_SRAM_SIZE           2048UL
#define AVR_DEVICE_MODEL        0x328U

#define AVR_EXTI_INT0           0U
#define AVR_EXTI_INT1           1U
#define AVR_EXTI_PCINT0         2U
#define AVR_EXTI_PCINT1         3U
#define AVR_EXTI_PCINT2         4U

/* ---------------- 毫秒时基 ---------------- */
static volatile uint32_t s_millis = 0U;

ISR(TIMER0_OVF_vect)
{
    s_millis++;
}

/* ---------------- GPIO 寄存器表 ---------------- */
static volatile uint8_t *const s_ddr[AVR_GPIO_PORT_COUNT] = { &DDRB, &DDRC, &DDRD };
static volatile uint8_t *const s_port_reg[AVR_GPIO_PORT_COUNT] = { &PORTB, &PORTC, &PORTD };
static volatile uint8_t *const s_pin_reg[AVR_GPIO_PORT_COUNT] = { &PINB, &PINC, &PIND };

/* ---------------- EXTI 状态 ---------------- */
typedef struct {
    port_exti_callback_t callback;
    void *user_data;
    port_pin_t pin;
    port_exti_trigger_t trigger;
    bool enabled;
} avr_exti_entry_t;

static avr_exti_entry_t s_exti[AVR_GPIO_PORT_COUNT][8];
static uint8_t s_pcint_last[AVR_GPIO_PORT_COUNT];

/* ---------------- 定时器占用表 ---------------- */
static port_tim_handle_t *s_tim_owner[3] = { NULL, NULL, NULL };
static port_pwm_handle_t *s_pwm_owner[3] = { NULL, NULL, NULL };

static port_capture_handle_t *s_capture_owner = NULL;
static volatile uint16_t s_capture_value = 0U;

/* ---------------- 软件 RTC ---------------- */
static port_rtc_time_t s_rtc_base;
static uint32_t s_rtc_base_tick = 0U;
static bool s_rtc_valid = false;

/* ---------------- 软件 RNG ---------------- */
static uint32_t s_rng_state = 0x12345678UL;

/* ---------------- 通用辅助 ---------------- */
static bool avr_pin_valid(port_pin_t pin)
{
    return (PORT_PIN_GET_PORT(pin) < AVR_GPIO_PORT_COUNT) &&
           (PORT_PIN_GET_INDEX(pin) < 8U);
}

static port_status_t avr_base_init(port_handle_base_t *base, void *instance)
{
    if (base->initialized) {
        return PORT_ALREADY_INIT;
    }
    base->instance = instance;
    base->priv = NULL;
    base->state = PORT_STATE_READY;
    base->error = PORT_OK;
    base->initialized = true;
    return PORT_OK;
}

static port_status_t avr_base_check(const port_handle_base_t *base)
{
    return base->initialized ? PORT_OK : PORT_NOT_READY;
}

static void avr_base_reset(port_handle_base_t *base)
{
    base->instance = NULL;
    base->priv = NULL;
    base->state = PORT_STATE_RESET;
    base->error = PORT_ERROR;
    base->initialized = false;
}

static bool avr_timeout(uint32_t start, uint32_t timeout)
{
    if (timeout == PORT_TIMEOUT_FOREVER) {
        return false;
    }
    return (uint32_t)(s_millis - start) >= timeout;
}

/* ---------------- Platform ---------------- */
static port_status_t avr_platform_init(void)
{
    /* Timer0：clk/64，溢出周期 1.024 ms（16 MHz），作为毫秒时基。 */
    TCCR0A = 0U;
    TCCR0B = (uint8_t)((1U << CS01) | (1U << CS00));
    TIMSK0 |= (uint8_t)(1U << TOIE0);
    s_rng_state = 0x12345678UL ^ (uint32_t)s_millis;
    return PORT_OK;
}

static port_status_t avr_platform_deinit(void)
{
    TIMSK0 &= (uint8_t)~(1U << TOIE0);
    return PORT_OK;
}

static port_status_t avr_platform_get_info(port_mcu_info_t *info)
{
    if (info == NULL) {
        return PORT_INVALID_PARAM;
    }
    memset(info, 0, sizeof(*info));
    info->family = PORT_MCU_AVR;
    info->model = AVR_DEVICE_MODEL;
    info->revision = 0U;
    info->core_clock = (uint32_t)F_CPU;
    info->ahb_clock = (uint32_t)F_CPU;
    info->apb1_clock = (uint32_t)F_CPU;
    info->apb2_clock = (uint32_t)F_CPU;
    info->flash_size = AVR_FLASH_SIZE;
    info->sram_size = AVR_SRAM_SIZE;
    return PORT_OK;
}

/* ---------------- 核心 API ---------------- */
uint32_t port_tick_get(void)
{
    uint32_t value;

    cli();
    value = s_millis;
    sei();
    return value;
}

void port_delay_ms(uint32_t ms)
{
    while (ms > 0U) {
        _delay_ms(1.0);
        ms--;
    }
}

void port_delay_us(uint32_t us)
{
    while (us > 0U) {
        _delay_us(1.0);
        us--;
    }
}

void port_irq_enable(void)
{
    sei();
}

void port_irq_disable(void)
{
    cli();
}

void port_system_reset(void)
{
    cli();
    wdt_enable(WDTO_15MS);
    for (;;) {
        ;
    }
}

void port_system_sleep(void)
{
    set_sleep_mode(SLEEP_MODE_IDLE);
    sleep_enable();
    sleep_cpu();
    sleep_disable();
}

void port_system_deepsleep(void)
{
    set_sleep_mode(SLEEP_MODE_PWR_DOWN);
    sleep_enable();
    sleep_cpu();
    sleep_disable();
}

/* ---------------- OS Ops ---------------- */
static port_status_t avr_os_mutex_create(port_mutex_t *mutex)
{
    if (mutex == NULL) {
        return PORT_INVALID_PARAM;
    }
    *mutex = 1U;
    return PORT_OK;
}

static port_status_t avr_os_mutex_delete(port_mutex_t mutex)
{
    PORT_UNUSED(mutex);
    return PORT_OK;
}

static port_status_t avr_os_mutex_lock(port_mutex_t mutex, uint32_t timeout)
{
    PORT_UNUSED(mutex);
    PORT_UNUSED(timeout);
    return PORT_OK;
}

static port_status_t avr_os_mutex_unlock(port_mutex_t mutex)
{
    PORT_UNUSED(mutex);
    return PORT_OK;
}

static port_status_t avr_os_sem_create(port_semaphore_t *sem, uint32_t max_count,
                                       uint32_t initial_count)
{
    if (sem == NULL || initial_count > max_count) {
        return PORT_INVALID_PARAM;
    }
    *sem = (uintptr_t)initial_count;
    return PORT_OK;
}

static port_status_t avr_os_sem_wait(port_semaphore_t sem, uint32_t timeout)
{
    PORT_UNUSED(timeout);
    return (sem == 0U) ? PORT_TIMEOUT : PORT_OK;
}

static port_status_t avr_os_sem_post(port_semaphore_t sem)
{
    PORT_UNUSED(sem);
    return PORT_OK;
}

static uint32_t avr_os_tick_get(void)
{
    return port_tick_get();
}

static void avr_os_delay_ms(uint32_t ms)
{
    port_delay_ms(ms);
}

static void avr_os_critical_enter(void)
{
    cli();
}

static void avr_os_critical_exit(void)
{
    sei();
}

/* ---------------- 时钟 ---------------- */
port_status_t port_clock_init(const port_clock_config_t *config)
{
    if (config == NULL) {
        return PORT_INVALID_PARAM;
    }
    /* ATmega328P 时钟由熔丝位/CKDIV 决定，运行时不可重配。 */
    return PORT_OK;
}

uint32_t port_clock_get_frequency(port_clock_bus_t bus)
{
    PORT_UNUSED(bus);
    return (uint32_t)F_CPU;
}

port_status_t port_clock_enable(void *peripheral)
{
    return (peripheral == NULL) ? PORT_INVALID_PARAM : PORT_OK;
}

port_status_t port_clock_disable(void *peripheral)
{
    return (peripheral == NULL) ? PORT_INVALID_PARAM : PORT_OK;
}

/* ---------------- 引脚复用 ---------------- */
port_status_t port_pin_init(const port_pin_config_t *config)
{
    uint8_t port;
    uint8_t bit;

    if (config == NULL || !avr_pin_valid(config->pin)) {
        return PORT_INVALID_PARAM;
    }
    if (config->function == PORT_PIN_FUNCTION_AF) {
        /* AVR 复用功能固定在特定引脚，不支持任意映射。 */
        return PORT_NOT_SUPPORTED;
    }
    port = (uint8_t)PORT_PIN_GET_PORT(config->pin);
    bit = (uint8_t)PORT_PIN_GET_INDEX(config->pin);
    *s_ddr[port] &= (uint8_t)~(1U << bit);
    *s_port_reg[port] &= (uint8_t)~(1U << bit);
    return PORT_OK;
}

port_status_t port_pin_deinit(port_pin_t pin)
{
    uint8_t port;
    uint8_t bit;

    if (!avr_pin_valid(pin)) {
        return PORT_INVALID_PARAM;
    }
    port = (uint8_t)PORT_PIN_GET_PORT(pin);
    bit = (uint8_t)PORT_PIN_GET_INDEX(pin);
    *s_ddr[port] &= (uint8_t)~(1U << bit);
    *s_port_reg[port] &= (uint8_t)~(1U << bit);
    return PORT_OK;
}

/* ---------------- GPIO ---------------- */
port_status_t port_gpio_init(port_pin_t pin, const port_gpio_config_t *config)
{
    uint8_t port;
    uint8_t bit;

    if (config == NULL || !avr_pin_valid(pin)) {
        return PORT_INVALID_PARAM;
    }
    port = (uint8_t)PORT_PIN_GET_PORT(pin);
    bit = (uint8_t)PORT_PIN_GET_INDEX(pin);

    switch (config->mode) {
    case PORT_GPIO_MODE_OUTPUT:
        *s_ddr[port] |= (uint8_t)(1U << bit);
        if (config->initial_level == PORT_GPIO_HIGH) {
            *s_port_reg[port] |= (uint8_t)(1U << bit);
        } else {
            *s_port_reg[port] &= (uint8_t)~(1U << bit);
        }
        break;
    case PORT_GPIO_MODE_INPUT:
        *s_ddr[port] &= (uint8_t)~(1U << bit);
        if (config->pull == PORT_GPIO_PULLUP) {
            *s_port_reg[port] |= (uint8_t)(1U << bit);
        } else {
            *s_port_reg[port] &= (uint8_t)~(1U << bit);
        }
        break;
    case PORT_GPIO_MODE_ANALOG:
        *s_ddr[port] &= (uint8_t)~(1U << bit);
        *s_port_reg[port] &= (uint8_t)~(1U << bit);
        break;
    case PORT_GPIO_MODE_AF:
    default:
        return PORT_NOT_SUPPORTED;
    }
    return PORT_OK;
}

port_status_t port_gpio_deinit(port_pin_t pin)
{
    return port_pin_deinit(pin);
}

port_status_t port_gpio_write(port_pin_t pin, port_gpio_level_t level)
{
    uint8_t port;
    uint8_t bit;

    if (!avr_pin_valid(pin)) {
        return PORT_INVALID_PARAM;
    }
    port = (uint8_t)PORT_PIN_GET_PORT(pin);
    bit = (uint8_t)PORT_PIN_GET_INDEX(pin);
    if (level == PORT_GPIO_HIGH) {
        *s_port_reg[port] |= (uint8_t)(1U << bit);
    } else {
        *s_port_reg[port] &= (uint8_t)~(1U << bit);
    }
    return PORT_OK;
}

port_status_t port_gpio_read(port_pin_t pin, port_gpio_level_t *level)
{
    uint8_t port;
    uint8_t bit;

    if (level == NULL || !avr_pin_valid(pin)) {
        return PORT_INVALID_PARAM;
    }
    port = (uint8_t)PORT_PIN_GET_PORT(pin);
    bit = (uint8_t)PORT_PIN_GET_INDEX(pin);
    *level = ((*s_pin_reg[port] & (uint8_t)(1U << bit)) != 0U) ? PORT_GPIO_HIGH
                                                               : PORT_GPIO_LOW;
    return PORT_OK;
}

port_status_t port_gpio_toggle(port_pin_t pin)
{
    uint8_t port;
    uint8_t bit;

    if (!avr_pin_valid(pin)) {
        return PORT_INVALID_PARAM;
    }
    port = (uint8_t)PORT_PIN_GET_PORT(pin);
    bit = (uint8_t)PORT_PIN_GET_INDEX(pin);
    *s_pin_reg[port] = (uint8_t)(1U << bit);   /* 写 PINx 翻转 */
    return PORT_OK;
}

port_status_t port_gpio_set(port_pin_t pin)
{
    return port_gpio_write(pin, PORT_GPIO_HIGH);
}

port_status_t port_gpio_reset(port_pin_t pin)
{
    return port_gpio_write(pin, PORT_GPIO_LOW);
}

/* ---------------- EXTI ---------------- */
static void avr_exti_apply_int(uint8_t line, port_exti_trigger_t trigger)
{
    uint8_t isc;

    switch (trigger) {
    case PORT_EXTI_RISING:  isc = 0x03U; break;
    case PORT_EXTI_FALLING: isc = 0x02U; break;
    case PORT_EXTI_BOTH:    isc = 0x01U; break;
    default:                isc = 0x00U; break;
    }
    if (line == 0U) {
        EICRA = (uint8_t)((EICRA & 0xFCU) | isc);
    } else {
        EICRA = (uint8_t)((EICRA & 0xF3U) | (uint8_t)(isc << 2));
    }
}

port_status_t port_exti_init(port_pin_t pin, const port_exti_config_t *config)
{
    uint8_t port;
    uint8_t bit;
    uint8_t group;

    if (config == NULL || !avr_pin_valid(pin)) {
        return PORT_INVALID_PARAM;
    }
    port = (uint8_t)PORT_PIN_GET_PORT(pin);
    bit = (uint8_t)PORT_PIN_GET_INDEX(pin);

    s_exti[port][bit].callback = config->callback;
    s_exti[port][bit].user_data = config->user_data;
    s_exti[port][bit].pin = pin;
    s_exti[port][bit].trigger = config->trigger;
    s_exti[port][bit].enabled = true;

    if (port == 2U && bit == 2U) {              /* INT0 */
        avr_exti_apply_int(0U, config->trigger);
        EIMSK |= (uint8_t)(1U << INT0);
        EIFR = (uint8_t)(1U << INTF0);
    } else if (port == 2U && bit == 3U) {       /* INT1 */
        avr_exti_apply_int(1U, config->trigger);
        EIMSK |= (uint8_t)(1U << INT1);
        EIFR = (uint8_t)(1U << INTF1);
    } else {
        group = port;
        if (group == 0U) {
            PCMSK0 |= (uint8_t)(1U << bit);
        } else if (group == 1U) {
            PCMSK1 |= (uint8_t)(1U << bit);
        } else {
            PCMSK2 |= (uint8_t)(1U << bit);
        }
        s_pcint_last[port] = *s_pin_reg[port];
        PCIFR = (uint8_t)(1U << group);
        PCICR |= (uint8_t)(1U << group);
    }
    return PORT_OK;
}

port_status_t port_exti_enable(port_pin_t pin)
{
    uint8_t port;
    uint8_t bit;

    if (!avr_pin_valid(pin)) {
        return PORT_INVALID_PARAM;
    }
    port = (uint8_t)PORT_PIN_GET_PORT(pin);
    bit = (uint8_t)PORT_PIN_GET_INDEX(pin);
    if (!s_exti[port][bit].enabled) {
        return PORT_NOT_READY;
    }
    if (port == 2U && bit == 2U) {
        EIMSK |= (uint8_t)(1U << INT0);
    } else if (port == 2U && bit == 3U) {
        EIMSK |= (uint8_t)(1U << INT1);
    } else {
        if (port == 0U) {
            PCMSK0 |= (uint8_t)(1U << bit);
        } else if (port == 1U) {
            PCMSK1 |= (uint8_t)(1U << bit);
        } else {
            PCMSK2 |= (uint8_t)(1U << bit);
        }
        s_pcint_last[port] = *s_pin_reg[port];
        PCICR |= (uint8_t)(1U << port);
    }
    return PORT_OK;
}

port_status_t port_exti_disable(port_pin_t pin)
{
    uint8_t port;
    uint8_t bit;

    if (!avr_pin_valid(pin)) {
        return PORT_INVALID_PARAM;
    }
    port = (uint8_t)PORT_PIN_GET_PORT(pin);
    bit = (uint8_t)PORT_PIN_GET_INDEX(pin);
    if (port == 2U && bit == 2U) {
        EIMSK &= (uint8_t)~(1U << INT0);
    } else if (port == 2U && bit == 3U) {
        EIMSK &= (uint8_t)~(1U << INT1);
    } else {
        if (port == 0U) {
            PCMSK0 &= (uint8_t)~(1U << bit);
        } else if (port == 1U) {
            PCMSK1 &= (uint8_t)~(1U << bit);
        } else {
            PCMSK2 &= (uint8_t)~(1U << bit);
        }
    }
    return PORT_OK;
}

port_status_t port_exti_clear(port_pin_t pin)
{
    uint8_t port;
    uint8_t bit;

    if (!avr_pin_valid(pin)) {
        return PORT_INVALID_PARAM;
    }
    port = (uint8_t)PORT_PIN_GET_PORT(pin);
    bit = (uint8_t)PORT_PIN_GET_INDEX(pin);
    if (port == 2U && bit == 2U) {
        EIFR = (uint8_t)(1U << INTF0);
    } else if (port == 2U && bit == 3U) {
        EIFR = (uint8_t)(1U << INTF1);
    } else {
        PCIFR = (uint8_t)(1U << port);
    }
    return PORT_OK;
}

port_status_t port_exti_deinit(port_pin_t pin)
{
    uint8_t port;
    uint8_t bit;

    if (!avr_pin_valid(pin)) {
        return PORT_INVALID_PARAM;
    }
    port = (uint8_t)PORT_PIN_GET_PORT(pin);
    bit = (uint8_t)PORT_PIN_GET_INDEX(pin);
    (void)port_exti_disable(pin);
    s_exti[port][bit].callback = NULL;
    s_exti[port][bit].user_data = NULL;
    s_exti[port][bit].enabled = false;
    return PORT_OK;
}

/*
 * vector: 0=INT0, 1=INT1, 2=PCINT0, 3=PCINT1, 4=PCINT2。
 * 由应用 ISR 调用，例如：
 *   ISR(INT0_vect) { port_avr_exti_irq(0); }
 *   ISR(PCINT0_vect) { port_avr_exti_irq(2); }
 */
void port_avr_exti_irq(uint8_t vector)
{
    uint8_t port;
    uint8_t bit;

    if (vector == AVR_EXTI_INT0) {
        if (s_exti[2][2].callback != NULL) {
            s_exti[2][2].callback(s_exti[2][2].pin, s_exti[2][2].user_data);
        }
        return;
    }
    if (vector == AVR_EXTI_INT1) {
        if (s_exti[2][3].callback != NULL) {
            s_exti[2][3].callback(s_exti[2][3].pin, s_exti[2][3].user_data);
        }
        return;
    }
    if (vector >= AVR_EXTI_PCINT0 && vector <= AVR_EXTI_PCINT2) {
        uint8_t changed;

        port = (uint8_t)(vector - AVR_EXTI_PCINT0);
        changed = (uint8_t)(*s_pin_reg[port] ^ s_pcint_last[port]);
        s_pcint_last[port] = *s_pin_reg[port];
        for (bit = 0U; bit < 8U; bit++) {
            if ((changed & (uint8_t)(1U << bit)) != 0U &&
                s_exti[port][bit].callback != NULL) {
                s_exti[port][bit].callback(s_exti[port][bit].pin,
                                           s_exti[port][bit].user_data);
            }
        }
    }
}

/* ---------------- UART0 ---------------- */
port_status_t port_uart_init(port_uart_handle_t *handle, const port_uart_config_t *config)
{
    uint32_t ubrr;
    uint8_t ucsrc = 0U;
    uint8_t ucsrb = 0U;

    if (handle == NULL || config == NULL || config->baudrate == 0U) {
        return PORT_INVALID_PARAM;
    }
    if (handle->base.initialized) {
        return PORT_ALREADY_INIT;
    }
    if (config->oversampling) {
        ubrr = (uint32_t)(F_CPU / (8UL * config->baudrate));
        if (ubrr > 0U) {
            ubrr--;
        }
        UCSR0A = (uint8_t)(1U << U2X0);
    } else {
        ubrr = (uint32_t)(F_CPU / (16UL * config->baudrate));
        if (ubrr > 0U) {
            ubrr--;
        }
        UCSR0A = 0U;
    }
    if (ubrr > 4095UL) {
        return PORT_INVALID_PARAM;
    }
    UBRR0H = (uint8_t)(ubrr >> 8);
    UBRR0L = (uint8_t)(ubrr & 0xFFU);

    switch (config->word_length) {
    case PORT_UART_WORD_7BIT:
        ucsrc |= (uint8_t)(1U << UCSZ01);
        break;
    case PORT_UART_WORD_9BIT:
        ucsrc |= (uint8_t)((1U << UCSZ01) | (1U << UCSZ00));
        ucsrb |= (uint8_t)(1U << UCSZ02);
        break;
    default:
        ucsrc |= (uint8_t)((1U << UCSZ01) | (1U << UCSZ00));
        break;
    }
    if (config->parity == PORT_UART_PARITY_EVEN) {
        ucsrc |= (uint8_t)(1U << UPM01);
    } else if (config->parity == PORT_UART_PARITY_ODD) {
        ucsrc |= (uint8_t)((1U << UPM01) | (1U << UPM00));
    }
    if (config->stop_bits == PORT_UART_STOP_2) {
        ucsrc |= (uint8_t)(1U << USBS0);
    }
    if (config->mode == PORT_UART_MODE_TX || config->mode == PORT_UART_MODE_TX_RX) {
        ucsrb |= (uint8_t)(1U << TXEN0);
    }
    if (config->mode == PORT_UART_MODE_RX || config->mode == PORT_UART_MODE_TX_RX) {
        ucsrb |= (uint8_t)(1U << RXEN0);
    }
    UCSR0C = ucsrc;
    UCSR0B = ucsrb;

    handle->config = *config;
    handle->tx_dma = NULL;
    handle->rx_dma = NULL;
    handle->callback = NULL;
    handle->user_data = NULL;
    return avr_base_init(&handle->base, NULL);
}

port_status_t port_uart_deinit(port_uart_handle_t *handle)
{
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != PORT_OK) {
        return PORT_NOT_READY;
    }
    UCSR0B = 0U;
    avr_base_reset(&handle->base);
    return PORT_OK;
}

port_status_t port_uart_transmit(port_uart_handle_t *handle, const uint8_t *data,
                                 size_t length, uint32_t timeout)
{
    size_t i;
    uint32_t start;

    if (handle == NULL || data == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != PORT_OK) {
        return PORT_NOT_READY;
    }
    start = s_millis;
    for (i = 0U; i < length; i++) {
        while ((UCSR0A & (uint8_t)(1U << UDRE0)) == 0U) {
            if (avr_timeout(start, (timeout == 0U) ? 1U : timeout)) {
                handle->base.error = PORT_TIMEOUT;
                return PORT_TIMEOUT;
            }
        }
        UDR0 = data[i];
    }
    return PORT_OK;
}

port_status_t port_uart_receive(port_uart_handle_t *handle, uint8_t *data,
                                size_t length, uint32_t timeout)
{
    size_t i;
    uint32_t start;

    if (handle == NULL || data == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != PORT_OK) {
        return PORT_NOT_READY;
    }
    start = s_millis;
    for (i = 0U; i < length; i++) {
        while ((UCSR0A & (uint8_t)(1U << RXC0)) == 0U) {
            if (avr_timeout(start, (timeout == 0U) ? 1U : timeout)) {
                handle->base.error = PORT_TIMEOUT;
                return PORT_TIMEOUT;
            }
        }
        data[i] = UDR0;
    }
    return PORT_OK;
}

port_status_t port_uart_transmit_it(port_uart_handle_t *handle, const uint8_t *data,
                                    size_t length)
{
    PORT_UNUSED(data);
    PORT_UNUSED(length);
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != PORT_OK) {
        return PORT_NOT_READY;
    }
    return PORT_NOT_SUPPORTED;
}

port_status_t port_uart_receive_it(port_uart_handle_t *handle, uint8_t *data,
                                   size_t length)
{
    PORT_UNUSED(data);
    PORT_UNUSED(length);
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != PORT_OK) {
        return PORT_NOT_READY;
    }
    return PORT_NOT_SUPPORTED;
}

port_status_t port_uart_transmit_dma(port_uart_handle_t *handle, const uint8_t *data,
                                     size_t length)
{
    PORT_UNUSED(data);
    PORT_UNUSED(length);
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != PORT_OK) {
        return PORT_NOT_READY;
    }
    return PORT_NOT_SUPPORTED;
}

port_status_t port_uart_receive_dma(port_uart_handle_t *handle, uint8_t *data,
                                    size_t length)
{
    PORT_UNUSED(data);
    PORT_UNUSED(length);
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != PORT_OK) {
        return PORT_NOT_READY;
    }
    return PORT_NOT_SUPPORTED;
}

port_status_t port_uart_receive_to_idle(port_uart_handle_t *handle, uint8_t *data,
                                        size_t length, port_transfer_mode_t mode)
{
    PORT_UNUSED(data);
    PORT_UNUSED(length);
    PORT_UNUSED(mode);
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != PORT_OK) {
        return PORT_NOT_READY;
    }
    return PORT_NOT_SUPPORTED;
}

size_t port_uart_get_rx_count(port_uart_handle_t *handle)
{
    if (handle == NULL || !handle->base.initialized) {
        return 0U;
    }
    return ((UCSR0A & (uint8_t)(1U << RXC0)) != 0U) ? 1U : 0U;
}

port_status_t port_uart_abort_tx(port_uart_handle_t *handle)
{
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != PORT_OK) {
        return PORT_NOT_READY;
    }
    handle->base.state = PORT_STATE_READY;
    return PORT_OK;
}

port_status_t port_uart_abort_rx(port_uart_handle_t *handle)
{
    return port_uart_abort_tx(handle);
}

port_status_t port_uart_abort(port_uart_handle_t *handle)
{
    return port_uart_abort_tx(handle);
}

port_state_t port_uart_get_state(port_uart_handle_t *handle)
{
    return (handle != NULL) ? handle->base.state : PORT_STATE_RESET;
}

port_status_t port_uart_get_error(port_uart_handle_t *handle)
{
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    return handle->base.error;
}

port_status_t port_uart_register_callback(port_uart_handle_t *handle,
                                          port_uart_callback_t callback, void *user_data)
{
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != PORT_OK) {
        return PORT_NOT_READY;
    }
    handle->callback = callback;
    handle->user_data = user_data;
    return PORT_OK;
}

/* ---------------- SPI ---------------- */
static uint8_t avr_spi_clock_bits(uint32_t baudrate, bool *double_speed)
{
    static const uint16_t divisors[] = { 2U, 4U, 8U, 16U, 32U, 64U, 128U };
    uint32_t required;
    uint8_t i;

    *double_speed = false;
    if (baudrate == 0U) {
        return 0U;
    }
    required = (uint32_t)F_CPU / baudrate;
    for (i = 0U; i < 7U; i++) {
        if (divisors[i] >= required) {
            switch (i) {
            case 0U: *double_speed = true;  return 0U;  /* /2  */
            case 1U: *double_speed = false; return 0U;  /* /4  */
            case 2U: *double_speed = true;  return 1U;  /* /8  */
            case 3U: *double_speed = false; return 1U;  /* /16 */
            case 4U: *double_speed = true;  return 2U;  /* /32 */
            case 5U: *double_speed = false; return 2U;  /* /64 */
            default: *double_speed = false; return 3U;  /* /128 */
            }
        }
    }
    return 3U;
}

static void avr_spi_apply_clock(uint32_t baudrate)
{
    bool double_speed = false;
    uint8_t spr = avr_spi_clock_bits(baudrate, &double_speed);

    SPCR = (uint8_t)((SPCR & (uint8_t)~((1U << SPR1) | (1U << SPR0))) |
                     (uint8_t)(spr << SPR0));
    if (double_speed) {
        SPSR |= (uint8_t)(1U << SPI2X);
    } else {
        SPSR &= (uint8_t)~(1U << SPI2X);
    }
}

static port_status_t avr_spi_wait(uint32_t start, uint32_t timeout)
{
    while ((SPSR & (uint8_t)(1U << SPIF)) == 0U) {
        if (avr_timeout(start, (timeout == 0U) ? 1U : timeout)) {
            return PORT_TIMEOUT;
        }
    }
    return PORT_OK;
}

port_status_t port_spi_init(port_spi_handle_t *handle, const port_spi_config_t *config)
{
    uint8_t spcr = (uint8_t)(1U << SPE);
    bool double_speed = false;
    uint8_t spr;

    if (handle == NULL || config == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (config->data_bits != 8U) {
        return PORT_NOT_SUPPORTED;
    }
    if (handle->base.initialized) {
        return PORT_ALREADY_INIT;
    }
    if (config->mode == PORT_SPI_MODE_MASTER) {
        DDRB |= (uint8_t)((1U << PB3) | (1U << PB5) | (1U << PB2));
        PORTB |= (uint8_t)(1U << PB2);          /* SS 保持高 */
        spcr |= (uint8_t)(1U << MSTR);
        spr = avr_spi_clock_bits(config->baudrate, &double_speed);
        spcr |= (uint8_t)(spr << SPR0);
    } else {
        DDRB |= (uint8_t)(1U << PB4);           /* MISO 输出 */
        DDRB &= (uint8_t)~((1U << PB3) | (1U << PB5));
        spcr &= (uint8_t)~(1U << MSTR);
    }
    if (config->cpol == PORT_SPI_CPOL_HIGH) {
        spcr |= (uint8_t)(1U << CPOL);
    }
    if (config->cpha == PORT_SPI_CPHA_2EDGE) {
        spcr |= (uint8_t)(1U << CPHA);
    }
    if (config->bit_order == PORT_SPI_BITORDER_LSB) {
        spcr |= (uint8_t)(1U << DORD);
    }
    SPCR = spcr;
    if (double_speed) {
        SPSR |= (uint8_t)(1U << SPI2X);
    } else {
        SPSR &= (uint8_t)~(1U << SPI2X);
    }
    (void)SPSR;
    (void)SPDR;

    handle->config = *config;
    handle->tx_dma = NULL;
    handle->rx_dma = NULL;
    handle->callback = NULL;
    handle->user_data = NULL;
    return avr_base_init(&handle->base, NULL);
}

port_status_t port_spi_deinit(port_spi_handle_t *handle)
{
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != PORT_OK) {
        return PORT_NOT_READY;
    }
    SPCR &= (uint8_t)~(1U << SPE);
    avr_base_reset(&handle->base);
    return PORT_OK;
}

port_status_t port_spi_transmit(port_spi_handle_t *handle, const void *data,
                                size_t length, uint32_t timeout)
{
    const uint8_t *source = (const uint8_t *)data;
    size_t i;
    uint32_t start;
    port_status_t status;

    if (handle == NULL || data == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != PORT_OK) {
        return PORT_NOT_READY;
    }
    start = s_millis;
    for (i = 0U; i < length; i++) {
        SPDR = source[i];
        status = avr_spi_wait(start, timeout);
        if (status != PORT_OK) {
            return status;
        }
        (void)SPSR;
        (void)SPDR;
    }
    return PORT_OK;
}

port_status_t port_spi_receive(port_spi_handle_t *handle, void *data,
                               size_t length, uint32_t timeout)
{
    uint8_t *dest = (uint8_t *)data;
    size_t i;
    uint32_t start;
    port_status_t status;

    if (handle == NULL || data == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != PORT_OK) {
        return PORT_NOT_READY;
    }
    start = s_millis;
    for (i = 0U; i < length; i++) {
        SPDR = 0xFFU;
        status = avr_spi_wait(start, timeout);
        if (status != PORT_OK) {
            return status;
        }
        dest[i] = SPDR;
    }
    return PORT_OK;
}

port_status_t port_spi_transfer(port_spi_handle_t *handle, const void *tx_data,
                                void *rx_data, size_t length, uint32_t timeout)
{
    const uint8_t *source = (const uint8_t *)tx_data;
    uint8_t *dest = (uint8_t *)rx_data;
    size_t i;
    uint32_t start;
    port_status_t status;

    if (handle == NULL || tx_data == NULL || rx_data == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != PORT_OK) {
        return PORT_NOT_READY;
    }
    start = s_millis;
    for (i = 0U; i < length; i++) {
        SPDR = source[i];
        status = avr_spi_wait(start, timeout);
        if (status != PORT_OK) {
            return status;
        }
        dest[i] = SPDR;
    }
    return PORT_OK;
}

port_status_t port_spi_transmit_dma(port_spi_handle_t *handle, const void *data,
                                    size_t length)
{
    PORT_UNUSED(data);
    PORT_UNUSED(length);
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != PORT_OK) {
        return PORT_NOT_READY;
    }
    return PORT_NOT_SUPPORTED;
}

port_status_t port_spi_receive_dma(port_spi_handle_t *handle, void *data,
                                   size_t length)
{
    PORT_UNUSED(data);
    PORT_UNUSED(length);
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != PORT_OK) {
        return PORT_NOT_READY;
    }
    return PORT_NOT_SUPPORTED;
}

port_status_t port_spi_transfer_dma(port_spi_handle_t *handle, const void *tx_data,
                                    void *rx_data, size_t length)
{
    PORT_UNUSED(tx_data);
    PORT_UNUSED(rx_data);
    PORT_UNUSED(length);
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != PORT_OK) {
        return PORT_NOT_READY;
    }
    return PORT_NOT_SUPPORTED;
}

port_status_t port_spi_set_speed(port_spi_handle_t *handle, uint32_t baudrate)
{
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != PORT_OK) {
        return PORT_NOT_READY;
    }
    if (baudrate == 0U) {
        return PORT_INVALID_PARAM;
    }
    avr_spi_apply_clock(baudrate);
    handle->config.baudrate = baudrate;
    return PORT_OK;
}

port_status_t port_spi_abort(port_spi_handle_t *handle)
{
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != PORT_OK) {
        return PORT_NOT_READY;
    }
    SPCR &= (uint8_t)~(1U << SPE);
    handle->base.state = PORT_STATE_READY;
    return PORT_OK;
}

port_state_t port_spi_get_state(port_spi_handle_t *handle)
{
    return (handle != NULL) ? handle->base.state : PORT_STATE_RESET;
}

/* ---------------- I2C（TWI） ---------------- */
static port_status_t avr_i2c_wait(uint32_t timeout, uint8_t *status)
{
    uint32_t start = s_millis;
    uint32_t effective = (timeout == 0U) ? 1U : timeout;

    while ((TWCR & (uint8_t)(1U << TWINT)) == 0U) {
        if (avr_timeout(start, effective)) {
            return PORT_TIMEOUT;
        }
    }
    if (status != NULL) {
        *status = (uint8_t)(TWSR & 0xF8U);
    }
    return PORT_OK;
}

static port_status_t avr_i2c_start(uint32_t timeout, uint8_t *status)
{
    TWCR = (uint8_t)((1U << TWINT) | (1U << TWSTA) | (1U << TWEN));
    return avr_i2c_wait(timeout, status);
}

static void avr_i2c_stop(void)
{
    TWCR = (uint8_t)((1U << TWINT) | (1U << TWSTO) | (1U << TWEN));
}

static port_status_t avr_i2c_write(uint8_t byte, uint8_t expect, uint32_t timeout)
{
    uint8_t status = 0U;
    port_status_t result;

    TWDR = byte;
    TWCR = (uint8_t)((1U << TWINT) | (1U << TWEN));
    result = avr_i2c_wait(timeout, &status);
    if (result != PORT_OK) {
        return result;
    }
    return (status == expect) ? PORT_OK : PORT_ERROR;
}

static port_status_t avr_i2c_read(uint8_t *byte, bool ack, uint32_t timeout)
{
    uint8_t status = 0U;
    port_status_t result;

    TWCR = (uint8_t)((1U << TWINT) | (1U << TWEN) | (ack ? (1U << TWEA) : 0U));
    result = avr_i2c_wait(timeout, &status);
    if (result != PORT_OK) {
        return result;
    }
    *byte = TWDR;
    if (ack && status != 0x50U) {
        return PORT_ERROR;
    }
    if (!ack && status != 0x58U) {
        return PORT_ERROR;
    }
    return PORT_OK;
}

port_status_t port_i2c_init(port_i2c_handle_t *handle, const port_i2c_config_t *config)
{
    uint32_t scl = 100000UL;
    uint32_t twbr;

    if (handle == NULL || config == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (config->address_mode == PORT_I2C_ADDR_10BIT) {
        return PORT_NOT_SUPPORTED;
    }
    if (handle->base.initialized) {
        return PORT_ALREADY_INIT;
    }
    if (config->speed == PORT_I2C_SPEED_FAST) {
        scl = 400000UL;
    }
    twbr = ((uint32_t)F_CPU / scl);
    twbr = (twbr > 16UL) ? ((twbr - 16UL) / 2UL) : 0UL;
    if (twbr < 10UL) {
        twbr = 10UL;
    }
    if (twbr > 255UL) {
        twbr = 255UL;
    }
    TWSR = 0U;                                  /* 预分频 1 */
    TWBR = (uint8_t)twbr;
    TWCR = (uint8_t)(1U << TWEN);

    handle->config = *config;
    handle->tx_dma = NULL;
    handle->rx_dma = NULL;
    handle->callback = NULL;
    handle->user_data = NULL;
    return avr_base_init(&handle->base, NULL);
}

port_status_t port_i2c_deinit(port_i2c_handle_t *handle)
{
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != PORT_OK) {
        return PORT_NOT_READY;
    }
    TWCR = 0U;
    avr_base_reset(&handle->base);
    return PORT_OK;
}

port_status_t port_i2c_master_transmit(port_i2c_handle_t *handle, uint16_t address,
                                       const uint8_t *data, size_t length,
                                       uint32_t timeout)
{
    uint8_t status = 0U;
    port_status_t result;
    size_t i;

    if (handle == NULL || data == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != PORT_OK) {
        return PORT_NOT_READY;
    }
    if (length == 0U) {
        return PORT_INVALID_PARAM;
    }
    result = avr_i2c_start(timeout, &status);
    if (result != PORT_OK || (status != 0x08U && status != 0x10U)) {
        avr_i2c_stop();
        return (result != PORT_OK) ? result : PORT_ERROR;
    }
    result = avr_i2c_write((uint8_t)(address << 1), 0x18U, timeout);
    if (result != PORT_OK) {
        avr_i2c_stop();
        return result;
    }
    for (i = 0U; i < length; i++) {
        result = avr_i2c_write(data[i], 0x28U, timeout);
        if (result != PORT_OK) {
            avr_i2c_stop();
            return result;
        }
    }
    avr_i2c_stop();
    return PORT_OK;
}

port_status_t port_i2c_master_receive(port_i2c_handle_t *handle, uint16_t address,
                                      uint8_t *data, size_t length, uint32_t timeout)
{
    uint8_t status = 0U;
    port_status_t result;
    size_t i;

    if (handle == NULL || data == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != PORT_OK) {
        return PORT_NOT_READY;
    }
    if (length == 0U) {
        return PORT_INVALID_PARAM;
    }
    result = avr_i2c_start(timeout, &status);
    if (result != PORT_OK || (status != 0x08U && status != 0x10U)) {
        avr_i2c_stop();
        return (result != PORT_OK) ? result : PORT_ERROR;
    }
    result = avr_i2c_write((uint8_t)((address << 1) | 1U), 0x40U, timeout);
    if (result != PORT_OK) {
        avr_i2c_stop();
        return result;
    }
    for (i = 0U; i < length; i++) {
        result = avr_i2c_read(&data[i], (i + 1U) < length, timeout);
        if (result != PORT_OK) {
            avr_i2c_stop();
            return result;
        }
    }
    avr_i2c_stop();
    return PORT_OK;
}

port_status_t port_i2c_master_transfer(port_i2c_handle_t *handle, uint16_t address,
                                       const uint8_t *tx_data, size_t tx_length,
                                       uint8_t *rx_data, size_t rx_length,
                                       uint32_t timeout)
{
    uint8_t status = 0U;
    port_status_t result;
    size_t i;

    if (handle == NULL || (tx_data == NULL && rx_data == NULL)) {
        return PORT_INVALID_PARAM;
    }
    if (tx_length == 0U && rx_length == 0U) {
        return PORT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != PORT_OK) {
        return PORT_NOT_READY;
    }
    result = avr_i2c_start(timeout, &status);
    if (result != PORT_OK || (status != 0x08U && status != 0x10U)) {
        avr_i2c_stop();
        return (result != PORT_OK) ? result : PORT_ERROR;
    }
    result = avr_i2c_write((uint8_t)(address << 1), 0x18U, timeout);
    if (result != PORT_OK) {
        avr_i2c_stop();
        return result;
    }
    for (i = 0U; i < tx_length; i++) {
        result = avr_i2c_write(tx_data[i], 0x28U, timeout);
        if (result != PORT_OK) {
            avr_i2c_stop();
            return result;
        }
    }
    if (rx_length > 0U) {
        result = avr_i2c_start(timeout, &status);
        if (result != PORT_OK || status != 0x10U) {
            avr_i2c_stop();
            return (result != PORT_OK) ? result : PORT_ERROR;
        }
        result = avr_i2c_write((uint8_t)((address << 1) | 1U), 0x40U, timeout);
        if (result != PORT_OK) {
            avr_i2c_stop();
            return result;
        }
        for (i = 0U; i < rx_length; i++) {
            result = avr_i2c_read(&rx_data[i], (i + 1U) < rx_length, timeout);
            if (result != PORT_OK) {
                avr_i2c_stop();
                return result;
            }
        }
    }
    avr_i2c_stop();
    return PORT_OK;
}

port_status_t port_i2c_mem_write(port_i2c_handle_t *handle, uint16_t address,
                                 uint16_t reg, uint8_t reg_size,
                                 const uint8_t *data, size_t length, uint32_t timeout)
{
    uint8_t status = 0U;
    port_status_t result;
    size_t i;

    if (handle == NULL || data == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != PORT_OK) {
        return PORT_NOT_READY;
    }
    if (length == 0U || (reg_size != 8U && reg_size != 16U)) {
        return PORT_INVALID_PARAM;
    }
    result = avr_i2c_start(timeout, &status);
    if (result != PORT_OK || (status != 0x08U && status != 0x10U)) {
        avr_i2c_stop();
        return (result != PORT_OK) ? result : PORT_ERROR;
    }
    result = avr_i2c_write((uint8_t)(address << 1), 0x18U, timeout);
    if (result != PORT_OK) {
        avr_i2c_stop();
        return result;
    }
    if (reg_size == 16U) {
        result = avr_i2c_write((uint8_t)(reg >> 8), 0x28U, timeout);
        if (result != PORT_OK) {
            avr_i2c_stop();
            return result;
        }
    }
    result = avr_i2c_write((uint8_t)(reg & 0xFFU), 0x28U, timeout);
    if (result != PORT_OK) {
        avr_i2c_stop();
        return result;
    }
    for (i = 0U; i < length; i++) {
        result = avr_i2c_write(data[i], 0x28U, timeout);
        if (result != PORT_OK) {
            avr_i2c_stop();
            return result;
        }
    }
    avr_i2c_stop();
    return PORT_OK;
}

port_status_t port_i2c_mem_read(port_i2c_handle_t *handle, uint16_t address,
                                uint16_t reg, uint8_t reg_size,
                                uint8_t *data, size_t length, uint32_t timeout)
{
    uint8_t status = 0U;
    port_status_t result;
    size_t i;

    if (handle == NULL || data == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != PORT_OK) {
        return PORT_NOT_READY;
    }
    if (length == 0U || (reg_size != 8U && reg_size != 16U)) {
        return PORT_INVALID_PARAM;
    }
    result = avr_i2c_start(timeout, &status);
    if (result != PORT_OK || (status != 0x08U && status != 0x10U)) {
        avr_i2c_stop();
        return (result != PORT_OK) ? result : PORT_ERROR;
    }
    result = avr_i2c_write((uint8_t)(address << 1), 0x18U, timeout);
    if (result != PORT_OK) {
        avr_i2c_stop();
        return result;
    }
    if (reg_size == 16U) {
        result = avr_i2c_write((uint8_t)(reg >> 8), 0x28U, timeout);
        if (result != PORT_OK) {
            avr_i2c_stop();
            return result;
        }
    }
    result = avr_i2c_write((uint8_t)(reg & 0xFFU), 0x28U, timeout);
    if (result != PORT_OK) {
        avr_i2c_stop();
        return result;
    }
    result = avr_i2c_start(timeout, &status);
    if (result != PORT_OK || status != 0x10U) {
        avr_i2c_stop();
        return (result != PORT_OK) ? result : PORT_ERROR;
    }
    result = avr_i2c_write((uint8_t)((address << 1) | 1U), 0x40U, timeout);
    if (result != PORT_OK) {
        avr_i2c_stop();
        return result;
    }
    for (i = 0U; i < length; i++) {
        result = avr_i2c_read(&data[i], (i + 1U) < length, timeout);
        if (result != PORT_OK) {
            avr_i2c_stop();
            return result;
        }
    }
    avr_i2c_stop();
    return PORT_OK;
}

port_status_t port_i2c_master_transmit_dma(port_i2c_handle_t *handle, uint16_t address,
                                           const uint8_t *data, size_t length)
{
    PORT_UNUSED(address);
    PORT_UNUSED(data);
    PORT_UNUSED(length);
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != PORT_OK) {
        return PORT_NOT_READY;
    }
    return PORT_NOT_SUPPORTED;
}

port_status_t port_i2c_master_receive_dma(port_i2c_handle_t *handle, uint16_t address,
                                          uint8_t *data, size_t length)
{
    PORT_UNUSED(address);
    PORT_UNUSED(data);
    PORT_UNUSED(length);
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != PORT_OK) {
        return PORT_NOT_READY;
    }
    return PORT_NOT_SUPPORTED;
}

port_status_t port_i2c_is_device_ready(port_i2c_handle_t *handle, uint16_t address,
                                       uint32_t trials, uint32_t timeout)
{
    uint32_t attempt;

    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != PORT_OK) {
        return PORT_NOT_READY;
    }
    if (trials == 0U) {
        trials = 1U;
    }
    for (attempt = 0U; attempt < trials; attempt++) {
        uint8_t status = 0U;
        port_status_t result = avr_i2c_start(timeout, &status);

        if (result == PORT_OK && (status == 0x08U || status == 0x10U)) {
            result = avr_i2c_write((uint8_t)(address << 1), 0x18U, timeout);
        } else if (result == PORT_OK) {
            result = PORT_ERROR;
        }
        avr_i2c_stop();
        if (result == PORT_OK) {
            return PORT_OK;
        }
    }
    return PORT_ERROR;
}

port_status_t port_i2c_bus_recover(port_i2c_handle_t *handle)
{
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != PORT_OK) {
        return PORT_NOT_READY;
    }
    avr_i2c_stop();
    TWCR = (uint8_t)(1U << TWEN);
    return PORT_OK;
}

port_status_t port_i2c_abort(port_i2c_handle_t *handle)
{
    return port_i2c_bus_recover(handle);
}

port_state_t port_i2c_get_state(port_i2c_handle_t *handle)
{
    return (handle != NULL) ? handle->base.state : PORT_STATE_RESET;
}

/* ---------------- TIM ---------------- */
static uint8_t avr_tim_cs_bits(uint32_t prescaler)
{
    switch (prescaler) {
    case 1U:    return 1U;
    case 8U:    return 2U;
    case 64U:   return 3U;
    case 256U:  return 4U;
    case 1024U: return 5U;
    default:    return 3U;
    }
}

static uint8_t avr_tim_alloc(void)
{
    static const uint8_t order[3] = { 1U, 2U, 0U };
    uint8_t i;

    for (i = 0U; i < 3U; i++) {
        if (s_tim_owner[order[i]] == NULL) {
            return order[i];
        }
    }
    return 0xFFU;
}

static uint8_t avr_tim_index(const port_tim_handle_t *handle)
{
    uintptr_t value = (uintptr_t)handle->base.priv;
    return (value == 0U) ? 0xFFU : (uint8_t)(value - 1U);
}

static void avr_tim_set_period_reg(uint8_t index, uint32_t period)
{
    switch (index) {
    case 0U: OCR0A = (uint8_t)((period > 255U) ? 255U : period); break;
    case 1U:
        OCR1A = (uint16_t)((period > 65535U) ? 65535U : period);
        break;
    case 2U: OCR2A = (uint8_t)((period > 255U) ? 255U : period); break;
    default: break;
    }
}

port_status_t port_tim_init(port_tim_handle_t *handle, const port_tim_config_t *config)
{
    uint8_t index;
    uint8_t cs;

    if (handle == NULL || config == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (handle->base.initialized) {
        return PORT_ALREADY_INIT;
    }
    index = avr_tim_alloc();
    if (index == 0xFFU) {
        return PORT_NO_MEMORY;
    }
    cs = avr_tim_cs_bits(config->prescaler);
    switch (index) {
    case 0U:
        TCCR0A = 0U;
        TCCR0B = (uint8_t)(1U << WGM02);
        break;
    case 1U:
        TCCR1A = 0U;
        TCCR1B = (uint8_t)(1U << WGM12);
        break;
    default:
        TCCR2A = 0U;
        TCCR2B = (uint8_t)(1U << WGM22);
        break;
    }
    avr_tim_set_period_reg(index, config->period);
    PORT_UNUSED(cs);        /* 初始化时保持停止，start() 再设置 CS */
    switch (index) {
    case 0U: TCNT0 = 0U; TIMSK0 &= (uint8_t)~(1U << OCIE0A); break;
    case 1U: TCNT1 = 0U; TIMSK1 &= (uint8_t)~(1U << OCIE1A); break;
    default: TCNT2 = 0U; TIMSK2 &= (uint8_t)~(1U << OCIE2A); break;
    }
    s_tim_owner[index] = handle;
    handle->config = *config;
    handle->callback = NULL;
    handle->user_data = NULL;
    (void)avr_base_init(&handle->base, NULL);
    handle->base.priv = (void *)(uintptr_t)(index + 1U);
    return PORT_OK;
}

port_status_t port_tim_deinit(port_tim_handle_t *handle)
{
    uint8_t index;

    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != PORT_OK) {
        return PORT_NOT_READY;
    }
    index = avr_tim_index(handle);
    if (index < 3U) {
        (void)port_tim_stop(handle);
        s_tim_owner[index] = NULL;
    }
    avr_base_reset(&handle->base);
    return PORT_OK;
}

port_status_t port_tim_start(port_tim_handle_t *handle)
{
    uint8_t index;
    uint8_t cs;

    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != PORT_OK) {
        return PORT_NOT_READY;
    }
    index = avr_tim_index(handle);
    if (index >= 3U) {
        return PORT_NOT_READY;
    }
    cs = avr_tim_cs_bits(handle->config.prescaler);
    switch (index) {
    case 0U: TCCR0B = (uint8_t)((TCCR0B & 0xF8U) | cs); break;
    case 1U: TCCR1B = (uint8_t)((TCCR1B & 0xF8U) | cs); break;
    default: TCCR2B = (uint8_t)((TCCR2B & 0xF8U) | cs); break;
    }
    return PORT_OK;
}

port_status_t port_tim_stop(port_tim_handle_t *handle)
{
    uint8_t index;

    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != PORT_OK) {
        return PORT_NOT_READY;
    }
    index = avr_tim_index(handle);
    switch (index) {
    case 0U: TCCR0B &= (uint8_t)0xF8U; break;
    case 1U: TCCR1B &= (uint8_t)0xF8U; break;
    case 2U: TCCR2B &= (uint8_t)0xF8U; break;
    default: return PORT_NOT_READY;
    }
    return PORT_OK;
}

port_status_t port_tim_start_it(port_tim_handle_t *handle)
{
    uint8_t index;

    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != PORT_OK) {
        return PORT_NOT_READY;
    }
    index = avr_tim_index(handle);
    switch (index) {
    case 0U: TIMSK0 |= (uint8_t)(1U << OCIE0A); break;
    case 1U: TIMSK1 |= (uint8_t)(1U << OCIE1A); break;
    case 2U: TIMSK2 |= (uint8_t)(1U << OCIE2A); break;
    default: return PORT_NOT_READY;
    }
    return port_tim_start(handle);
}

port_status_t port_tim_stop_it(port_tim_handle_t *handle)
{
    uint8_t index;

    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != PORT_OK) {
        return PORT_NOT_READY;
    }
    index = avr_tim_index(handle);
    switch (index) {
    case 0U: TIMSK0 &= (uint8_t)~(1U << OCIE0A); break;
    case 1U: TIMSK1 &= (uint8_t)~(1U << OCIE1A); break;
    case 2U: TIMSK2 &= (uint8_t)~(1U << OCIE2A); break;
    default: return PORT_NOT_READY;
    }
    return port_tim_stop(handle);
}

port_status_t port_tim_set_period(port_tim_handle_t *handle, uint32_t period)
{
    uint8_t index;

    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != PORT_OK) {
        return PORT_NOT_READY;
    }
    index = avr_tim_index(handle);
    if (index >= 3U) {
        return PORT_NOT_READY;
    }
    avr_tim_set_period_reg(index, period);
    handle->config.period = period;
    return PORT_OK;
}

port_status_t port_tim_set_prescaler(port_tim_handle_t *handle, uint32_t prescaler)
{
    uint8_t index;
    uint8_t cs;

    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != PORT_OK) {
        return PORT_NOT_READY;
    }
    index = avr_tim_index(handle);
    if (index >= 3U) {
        return PORT_NOT_READY;
    }
    cs = avr_tim_cs_bits(prescaler);
    switch (index) {
    case 0U: TCCR0B = (uint8_t)((TCCR0B & 0xF8U) | cs); break;
    case 1U: TCCR1B = (uint8_t)((TCCR1B & 0xF8U) | cs); break;
    default: TCCR2B = (uint8_t)((TCCR2B & 0xF8U) | cs); break;
    }
    handle->config.prescaler = prescaler;
    return PORT_OK;
}

port_status_t port_tim_set_counter(port_tim_handle_t *handle, uint32_t counter)
{
    uint8_t index;

    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != PORT_OK) {
        return PORT_NOT_READY;
    }
    index = avr_tim_index(handle);
    switch (index) {
    case 0U: TCNT0 = (uint8_t)counter; break;
    case 1U: TCNT1 = (uint16_t)counter; break;
    case 2U: TCNT2 = (uint8_t)counter; break;
    default: return PORT_NOT_READY;
    }
    return PORT_OK;
}

uint32_t port_tim_get_counter(port_tim_handle_t *handle)
{
    uint8_t index;

    if (handle == NULL || !handle->base.initialized) {
        return 0U;
    }
    index = avr_tim_index(handle);
    switch (index) {
    case 0U: return (uint32_t)TCNT0;
    case 1U: return (uint32_t)TCNT1;
    case 2U: return (uint32_t)TCNT2;
    default: return 0U;
    }
}

static void avr_tim_irq(uint8_t index)
{
    port_tim_handle_t *handle = s_tim_owner[index];

    if (handle != NULL && handle->callback != NULL) {
        handle->callback(handle, PORT_EVENT_COMPLETE, handle->user_data);
    }
}

ISR(TIMER0_COMPA_vect)
{
    avr_tim_irq(0U);
}

ISR(TIMER1_COMPA_vect)
{
    avr_tim_irq(1U);
}

ISR(TIMER2_COMPA_vect)
{
    avr_tim_irq(2U);
}

/* ---------------- PWM ---------------- */
static uint8_t avr_pwm_alloc(void)
{
    static const uint8_t order[3] = { 1U, 2U, 0U };
    uint8_t i;

    for (i = 0U; i < 3U; i++) {
        if (s_pwm_owner[order[i]] == NULL) {
            return order[i];
        }
    }
    return 0xFFU;
}

static uint8_t avr_pwm_index(const port_pwm_handle_t *handle)
{
    uintptr_t value = (uintptr_t)handle->base.priv;
    return (value == 0U) ? 0xFFU : (uint8_t)(value - 1U);
}

static void avr_pwm_output_pin(uint8_t index, uint8_t channel)
{
    switch (index) {
    case 0U:
        if (channel == 1U) { DDRD |= (uint8_t)(1U << PD6); }
        else { DDRD |= (uint8_t)(1U << PD5); }
        break;
    case 1U:
        if (channel == 1U) { DDRB |= (uint8_t)(1U << PB1); }
        else { DDRB |= (uint8_t)(1U << PB2); }
        break;
    default:
        if (channel == 1U) { DDRB |= (uint8_t)(1U << PB3); }
        else { DDRD |= (uint8_t)(1U << PD3); }
        break;
    }
}

static uint8_t avr_pwm_com_bits(uint8_t channel, port_pwm_polarity_t polarity)
{
    uint8_t high_bit = (channel == 1U) ? (uint8_t)COM1A1 : (uint8_t)COM1B1;
    uint8_t bits = (uint8_t)(1U << high_bit);

    if (polarity == PORT_PWM_POLARITY_LOW) {
        bits |= (uint8_t)(1U << (high_bit - 1U));
    }
    return bits;
}

static uint8_t avr_pwm_clock_bits(uint32_t frequency)
{
    static const uint16_t divisors[5] = { 1U, 8U, 64U, 256U, 1024U };
    uint32_t required;
    uint8_t i;

    if (frequency == 0U) {
        return 3U;
    }
    required = (uint32_t)F_CPU / (frequency * 256UL);
    if (required == 0U) {
        required = 1U;
    }
    for (i = 0U; i < 5U; i++) {
        if (divisors[i] >= required) {
            return (uint8_t)(i + 1U);
        }
    }
    return 5U;
}

static void avr_pwm_write_duty(uint8_t index, uint8_t channel, uint32_t duty)
{
    uint8_t compare = (uint8_t)((duty * 255UL) / 10000UL);

    switch (index) {
    case 0U:
        if (channel == 1U) { OCR0A = compare; } else { OCR0B = compare; }
        break;
    case 1U:
        if (channel == 1U) {
            OCR1A = (uint16_t)((duty * 255UL) / 10000UL);
        } else {
            OCR1B = (uint16_t)((duty * 255UL) / 10000UL);
        }
        break;
    default:
        if (channel == 1U) { OCR2A = compare; } else { OCR2B = compare; }
        break;
    }
}

port_status_t port_pwm_init(port_pwm_handle_t *handle, const port_pwm_config_t *config)
{
    uint8_t index;
    uint8_t cs;
    uint8_t com;

    if (handle == NULL || config == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (config->frequency == 0U || config->duty > 10000U) {
        return PORT_INVALID_PARAM;
    }
    if (handle->channel < 1U || handle->channel > 2U) {
        handle->channel = 1U;
    }
    if (handle->base.initialized) {
        return PORT_ALREADY_INIT;
    }
    index = avr_pwm_alloc();
    if (index == 0xFFU) {
        return PORT_NO_MEMORY;
    }
    cs = avr_pwm_clock_bits(config->frequency);
    com = avr_pwm_com_bits(handle->channel, config->polarity);
    switch (index) {
    case 0U:
        TCCR0A = (uint8_t)((1U << WGM01) | (1U << WGM00) | com);
        TCCR0B = cs;
        break;
    case 1U:
        ICR1 = 255U;
        TCCR1A = (uint8_t)((1U << WGM11) | com);
        TCCR1B = (uint8_t)((1U << WGM13) | (1U << WGM12) | cs);
        break;
    default:
        TCCR2A = (uint8_t)((1U << WGM21) | (1U << WGM20) | com);
        TCCR2B = cs;
        break;
    }
    avr_pwm_output_pin(index, handle->channel);
    avr_pwm_write_duty(index, handle->channel, config->duty);
    s_pwm_owner[index] = handle;
    handle->config = *config;
    (void)avr_base_init(&handle->base, NULL);
    handle->base.priv = (void *)(uintptr_t)(index + 1U);
    return PORT_OK;
}

port_status_t port_pwm_deinit(port_pwm_handle_t *handle)
{
    uint8_t index;

    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != PORT_OK) {
        return PORT_NOT_READY;
    }
    index = avr_pwm_index(handle);
    if (index < 3U) {
        (void)port_pwm_stop(handle);
        switch (index) {
        case 0U: TCCR0A = 0U; break;
        case 1U: TCCR1A = 0U; break;
        default: TCCR2A = 0U; break;
        }
        s_pwm_owner[index] = NULL;
    }
    avr_base_reset(&handle->base);
    return PORT_OK;
}

port_status_t port_pwm_start(port_pwm_handle_t *handle)
{
    uint8_t index;
    uint8_t cs;

    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != PORT_OK) {
        return PORT_NOT_READY;
    }
    index = avr_pwm_index(handle);
    if (index >= 3U) {
        return PORT_NOT_READY;
    }
    cs = avr_pwm_clock_bits(handle->config.frequency);
    switch (index) {
    case 0U: TCCR0B = (uint8_t)((TCCR0B & 0xF8U) | cs); break;
    case 1U: TCCR1B = (uint8_t)((TCCR1B & 0xF8U) | cs); break;
    default: TCCR2B = (uint8_t)((TCCR2B & 0xF8U) | cs); break;
    }
    return PORT_OK;
}

port_status_t port_pwm_stop(port_pwm_handle_t *handle)
{
    uint8_t index;

    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != PORT_OK) {
        return PORT_NOT_READY;
    }
    index = avr_pwm_index(handle);
    switch (index) {
    case 0U: TCCR0B &= (uint8_t)0xF8U; break;
    case 1U: TCCR1B &= (uint8_t)0xF8U; break;
    case 2U: TCCR2B &= (uint8_t)0xF8U; break;
    default: return PORT_NOT_READY;
    }
    return PORT_OK;
}

port_status_t port_pwm_set_frequency(port_pwm_handle_t *handle, uint32_t frequency)
{
    uint8_t index;
    uint8_t cs;

    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != PORT_OK) {
        return PORT_NOT_READY;
    }
    if (frequency == 0U) {
        return PORT_INVALID_PARAM;
    }
    index = avr_pwm_index(handle);
    if (index >= 3U) {
        return PORT_NOT_READY;
    }
    cs = avr_pwm_clock_bits(frequency);
    switch (index) {
    case 0U: TCCR0B = (uint8_t)((TCCR0B & 0xF8U) | cs); break;
    case 1U: TCCR1B = (uint8_t)((TCCR1B & 0xF8U) | cs); break;
    default: TCCR2B = (uint8_t)((TCCR2B & 0xF8U) | cs); break;
    }
    handle->config.frequency = frequency;
    return PORT_OK;
}

port_status_t port_pwm_set_duty(port_pwm_handle_t *handle, uint32_t duty)
{
    uint8_t index;

    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != PORT_OK) {
        return PORT_NOT_READY;
    }
    if (duty > 10000U) {
        return PORT_INVALID_PARAM;
    }
    index = avr_pwm_index(handle);
    if (index >= 3U) {
        return PORT_NOT_READY;
    }
    avr_pwm_write_duty(index, handle->channel, duty);
    handle->config.duty = duty;
    return PORT_OK;
}

port_status_t port_pwm_set_polarity(port_pwm_handle_t *handle,
                                    port_pwm_polarity_t polarity)
{
    uint8_t index;
    uint8_t com;

    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != PORT_OK) {
        return PORT_NOT_READY;
    }
    index = avr_pwm_index(handle);
    if (index >= 3U) {
        return PORT_NOT_READY;
    }
    com = avr_pwm_com_bits(handle->channel, polarity);
    switch (index) {
    case 0U: TCCR0A = (uint8_t)((TCCR0A & 0x0FU) | com); break;
    case 1U: TCCR1A = (uint8_t)((TCCR1A & 0x0FU) | com); break;
    default: TCCR2A = (uint8_t)((TCCR2A & 0x0FU) | com); break;
    }
    handle->config.polarity = polarity;
    return PORT_OK;
}

/* ---------------- CAPTURE（Timer1 ICP1 = PB0） ---------------- */
port_status_t port_capture_init(port_capture_handle_t *handle,
                                const port_capture_config_t *config)
{
    uint8_t cs;

    if (handle == NULL || config == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (handle->base.initialized) {
        return PORT_ALREADY_INIT;
    }
    if (s_capture_owner != NULL) {
        return PORT_NO_MEMORY;
    }
    cs = avr_tim_cs_bits(config->prescaler);
    TCCR1A = 0U;
    TCCR1B = cs;
    if (config->edge == PORT_CAPTURE_RISING || config->edge == PORT_CAPTURE_BOTH) {
        TCCR1B |= (uint8_t)(1U << ICES1);
    }
    if (config->filter != 0U) {
        TCCR1B |= (uint8_t)(1U << ICNC1);
    }
    TCNT1 = 0U;
    s_capture_value = ICR1;
    TIMSK1 &= (uint8_t)~(1U << ICIE1);
    s_capture_owner = handle;
    handle->config = *config;
    handle->callback = NULL;
    handle->user_data = NULL;
    return avr_base_init(&handle->base, NULL);
}

port_status_t port_capture_start(port_capture_handle_t *handle)
{
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != PORT_OK) {
        return PORT_NOT_READY;
    }
    TIMSK1 |= (uint8_t)(1U << ICIE1);
    return PORT_OK;
}

port_status_t port_capture_stop(port_capture_handle_t *handle)
{
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != PORT_OK) {
        return PORT_NOT_READY;
    }
    TIMSK1 &= (uint8_t)~(1U << ICIE1);
    return PORT_OK;
}

port_status_t port_capture_start_it(port_capture_handle_t *handle)
{
    return port_capture_start(handle);
}

uint32_t port_capture_get_value(port_capture_handle_t *handle)
{
    if (handle == NULL || !handle->base.initialized) {
        return 0U;
    }
    return (uint32_t)s_capture_value;
}

ISR(TIMER1_CAPT_vect)
{
    s_capture_value = ICR1;
    if (s_capture_owner != NULL) {
        if (s_capture_owner->config.edge == PORT_CAPTURE_BOTH) {
            TCCR1B ^= (uint8_t)(1U << ICES1);
        }
        if (s_capture_owner->callback != NULL) {
            s_capture_owner->callback(s_capture_owner, PORT_EVENT_COMPLETE,
                                      s_capture_owner->user_data);
        }
    }
}

/* ---------------- ENCODER ---------------- */
port_status_t port_encoder_init(port_encoder_handle_t *handle,
                                const port_encoder_config_t *config)
{
    PORT_UNUSED(config);
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    return PORT_NOT_SUPPORTED;
}

port_status_t port_encoder_start(port_encoder_handle_t *handle)
{
    PORT_UNUSED(handle);
    return PORT_NOT_SUPPORTED;
}

port_status_t port_encoder_stop(port_encoder_handle_t *handle)
{
    PORT_UNUSED(handle);
    return PORT_NOT_SUPPORTED;
}

int32_t port_encoder_get_count(port_encoder_handle_t *handle)
{
    PORT_UNUSED(handle);
    return 0;
}

port_status_t port_encoder_set_count(port_encoder_handle_t *handle, int32_t count)
{
    PORT_UNUSED(handle);
    PORT_UNUSED(count);
    return PORT_NOT_SUPPORTED;
}

/* ---------------- WDG ---------------- */
static uint8_t s_wdg_bits = 0U;

static uint8_t avr_wdg_encode(uint8_t index)
{
    return (uint8_t)((index & 0x07U) | ((index & 0x08U) ? (1U << WDP3) : 0U));
}

static uint8_t avr_wdg_bits_for(uint32_t timeout_ms)
{
    static const uint32_t timeouts[10] = {
        16UL, 32UL, 64UL, 125UL, 250UL, 500UL, 1000UL, 2000UL, 4000UL, 8000UL
    };
    uint8_t i;

    for (i = 0U; i < 10U; i++) {
        if (timeouts[i] >= timeout_ms) {
            return avr_wdg_encode(i);
        }
    }
    return avr_wdg_encode(9U);
}

port_status_t port_wdg_init(port_wdg_handle_t *handle, const port_wdg_config_t *config)
{
    if (handle == NULL || config == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (config->type == PORT_WDG_WINDOW) {
        return PORT_NOT_SUPPORTED;
    }
    if (handle->base.initialized) {
        return PORT_ALREADY_INIT;
    }
    s_wdg_bits = avr_wdg_bits_for(config->timeout_ms);
    WDTCSR = s_wdg_bits;
    handle->config = *config;
    return avr_base_init(&handle->base, NULL);
}

port_status_t port_wdg_start(port_wdg_handle_t *handle)
{
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != PORT_OK) {
        return PORT_NOT_READY;
    }
    WDTCSR = (uint8_t)((1U << WDCE) | (1U << WDE));
    WDTCSR = (uint8_t)((1U << WDE) | s_wdg_bits);
    return PORT_OK;
}

port_status_t port_wdg_refresh(port_wdg_handle_t *handle)
{
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != PORT_OK) {
        return PORT_NOT_READY;
    }
    wdt_reset();
    return PORT_OK;
}

port_status_t port_wdg_stop(port_wdg_handle_t *handle)
{
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != PORT_OK) {
        return PORT_NOT_READY;
    }
    WDTCSR = (uint8_t)((1U << WDCE) | (1U << WDE));
    WDTCSR = 0U;
    return PORT_OK;
}

bool port_wdg_is_reset(void)
{
    return (MCUSR & (uint8_t)(1U << WDRF)) != 0U;
}

void port_wdg_clear_reset_flag(void)
{
    MCUSR &= (uint8_t)~(1U << WDRF);
}

/* ---------------- RTC（软件时间） ---------------- */
static uint32_t avr_epoch_days(uint16_t year, uint8_t month, uint8_t day)
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

static void avr_civil_from_days(uint32_t days, uint16_t *year, uint8_t *month,
                                uint8_t *day)
{
    int32_t z = (int32_t)days + 719468;
    int32_t era = (z >= 0 ? z : (z - 146096)) / 146097;
    uint32_t doe = (uint32_t)(z - era * 146097);
    uint32_t yoe = (doe - doe / 1460U + doe / 36524U - doe / 146096U) / 365U;
    int32_t y = (int32_t)yoe + era * 400;
    uint32_t doy = doe - (365U * yoe + yoe / 4U - yoe / 100U);
    uint32_t mp = (5U * doy + 2U) / 153U;
    uint32_t d = doy - (153U * mp + 2U) / 5U + 1U;
    uint32_t m = mp + ((mp < 10U) ? 3U : (uint32_t)-9);

    y += (m <= 2U) ? 1 : 0;
    *year = (uint16_t)y;
    *month = (uint8_t)m;
    *day = (uint8_t)d;
}

static void avr_rtc_apply_elapsed(port_rtc_time_t *time, uint32_t elapsed_seconds)
{
    uint32_t epoch = avr_epoch_days(s_rtc_base.year, s_rtc_base.month,
                                    s_rtc_base.day) * 86400UL;
    uint32_t total;
    uint32_t days;
    uint32_t seconds;

    epoch += (uint32_t)s_rtc_base.hour * 3600UL +
             (uint32_t)s_rtc_base.minute * 60UL + (uint32_t)s_rtc_base.second;
    total = epoch + elapsed_seconds;
    days = total / 86400UL;
    seconds = total % 86400UL;
    avr_civil_from_days(days, &time->year, &time->month, &time->day);
    time->hour = (uint8_t)(seconds / 3600UL);
    time->minute = (uint8_t)((seconds % 3600UL) / 60UL);
    time->second = (uint8_t)(seconds % 60UL);
    time->weekday = (uint8_t)(((days + 3UL) % 7UL) + 1UL);   /* 1=周一 */
}

port_status_t port_rtc_init(void)
{
    uint32_t days;

    if (s_rtc_valid) {
        return PORT_ALREADY_INIT;
    }
    s_rtc_base.year = 2000U;
    s_rtc_base.month = 1U;
    s_rtc_base.day = 1U;
    s_rtc_base.hour = 0U;
    s_rtc_base.minute = 0U;
    s_rtc_base.second = 0U;
    days = avr_epoch_days(2000U, 1U, 1U);
    s_rtc_base.weekday = (uint8_t)(((days + 3UL) % 7UL) + 1UL);
    s_rtc_base_tick = s_millis;
    s_rtc_valid = true;
    return PORT_OK;
}

port_status_t port_rtc_deinit(void)
{
    s_rtc_valid = false;
    return PORT_OK;
}

port_status_t port_rtc_set_time(const port_rtc_time_t *time)
{
    if (time == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (!s_rtc_valid) {
        return PORT_NOT_READY;
    }
    if (time->year < 2000U || time->month < 1U || time->month > 12U ||
        time->day < 1U || time->day > 31U || time->hour > 23U ||
        time->minute > 59U || time->second > 59U) {
        return PORT_INVALID_PARAM;
    }
    s_rtc_base = *time;
    s_rtc_base_tick = s_millis;
    return PORT_OK;
}

port_status_t port_rtc_get_time(port_rtc_time_t *time)
{
    uint32_t elapsed;

    if (time == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (!s_rtc_valid) {
        return PORT_NOT_READY;
    }
    elapsed = (uint32_t)(s_millis - s_rtc_base_tick) / 1000UL;
    avr_rtc_apply_elapsed(time, elapsed);
    return PORT_OK;
}

port_status_t port_rtc_set_alarm(port_rtc_alarm_t alarm,
                                 const port_rtc_alarm_config_t *config)
{
    PORT_UNUSED(alarm);
    PORT_UNUSED(config);
    if (!s_rtc_valid) {
        return PORT_NOT_READY;
    }
    return PORT_NOT_SUPPORTED;
}

port_status_t port_rtc_disable_alarm(port_rtc_alarm_t alarm)
{
    PORT_UNUSED(alarm);
    return PORT_NOT_SUPPORTED;
}

port_status_t port_rtc_get_timestamp(uint32_t *timestamp)
{
    port_rtc_time_t time;
    port_status_t status;

    if (timestamp == NULL) {
        return PORT_INVALID_PARAM;
    }
    status = port_rtc_get_time(&time);
    if (status != PORT_OK) {
        return status;
    }
    *timestamp = avr_epoch_days(time.year, time.month, time.day) * 86400UL +
                 (uint32_t)time.hour * 3600UL +
                 (uint32_t)time.minute * 60UL + (uint32_t)time.second;
    return PORT_OK;
}

/* ---------------- FLASH（仅读） ---------------- */
port_status_t port_flash_init(port_flash_handle_t *handle)
{
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (handle->base.initialized) {
        return PORT_ALREADY_INIT;
    }
    memset(&handle->info, 0, sizeof(handle->info));
    handle->info.start = 0U;
    handle->info.size = AVR_FLASH_SIZE;
    handle->info.erase_size = AVR_FLASH_PAGE_SIZE;
    handle->info.program_size = AVR_FLASH_PAGE_SIZE;
    handle->info.flags = PORT_FLASH_FLAG_READ;
    return avr_base_init(&handle->base, NULL);
}

port_status_t port_flash_deinit(port_flash_handle_t *handle)
{
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != PORT_OK) {
        return PORT_NOT_READY;
    }
    avr_base_reset(&handle->base);
    return PORT_OK;
}

port_status_t port_flash_get_info(port_flash_handle_t *handle, port_flash_info_t *info)
{
    if (handle == NULL || info == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != PORT_OK) {
        return PORT_NOT_READY;
    }
    *info = handle->info;
    return PORT_OK;
}

port_status_t port_flash_read(port_flash_handle_t *handle, uintptr_t address,
                              void *buffer, size_t length)
{
    uint8_t *dest = (uint8_t *)buffer;
    size_t i;

    if (handle == NULL || buffer == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != PORT_OK) {
        return PORT_NOT_READY;
    }
    if (length == 0U || address > AVR_FLASH_SIZE ||
        length > (size_t)(AVR_FLASH_SIZE - address)) {
        return PORT_INVALID_PARAM;
    }
    for (i = 0U; i < length; i++) {
        dest[i] = pgm_read_byte((const uint8_t *)(uintptr_t)(address + i));
    }
    return PORT_OK;
}

port_status_t port_flash_write(port_flash_handle_t *handle, uintptr_t address,
                               const void *buffer, size_t length)
{
    PORT_UNUSED(address);
    PORT_UNUSED(buffer);
    PORT_UNUSED(length);
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != PORT_OK) {
        return PORT_NOT_READY;
    }
    /* ATmega328P 的 SPM 只能在 bootloader 区执行，应用区无法自编程。 */
    return PORT_NOT_SUPPORTED;
}

port_status_t port_flash_erase(port_flash_handle_t *handle, uintptr_t address,
                               size_t length)
{
    PORT_UNUSED(address);
    PORT_UNUSED(length);
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != PORT_OK) {
        return PORT_NOT_READY;
    }
    return PORT_NOT_SUPPORTED;
}

port_status_t port_flash_lock(port_flash_handle_t *handle)
{
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != PORT_OK) {
        return PORT_NOT_READY;
    }
    return PORT_OK;
}

port_status_t port_flash_unlock(port_flash_handle_t *handle)
{
    return port_flash_lock(handle);
}

/* ---------------- CRC（软件） ---------------- */
static uint32_t avr_crc_reflect(uint32_t value, uint8_t bits)
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

static uint32_t avr_crc_software(const port_crc_config_t *config, const uint8_t *data,
                                 size_t length)
{
    uint32_t mask = (config->width == 32U) ? 0xFFFFFFFFUL
                                           : ((1UL << config->width) - 1UL);
    uint32_t top = 1UL << ((uint32_t)config->width - 1U);
    uint32_t crc = config->initial_value & mask;
    size_t i;

    for (i = 0U; i < length; i++) {
        uint8_t byte = data[i];
        uint8_t bit;

        if (config->input_reverse) {
            byte = (uint8_t)avr_crc_reflect(byte, 8U);
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
        crc = avr_crc_reflect(crc, (uint8_t)config->width);
    }
    return (crc ^ config->xor_output) & mask;
}

port_status_t port_crc_init(port_crc_handle_t *handle, const port_crc_config_t *config)
{
    if (handle == NULL || config == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (config->width != PORT_CRC_WIDTH_8 && config->width != PORT_CRC_WIDTH_16 &&
        config->width != PORT_CRC_WIDTH_32) {
        return PORT_INVALID_PARAM;
    }
    if (handle->base.initialized) {
        return PORT_ALREADY_INIT;
    }
    handle->config = *config;
    return avr_base_init(&handle->base, NULL);
}

port_status_t port_crc_reset(port_crc_handle_t *handle)
{
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != PORT_OK) {
        return PORT_NOT_READY;
    }
    return PORT_OK;
}

port_status_t port_crc_calculate(port_crc_handle_t *handle, const void *data,
                                 size_t length, uint32_t *result)
{
    if (handle == NULL || data == NULL || result == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != PORT_OK) {
        return PORT_NOT_READY;
    }
    *result = avr_crc_software(&handle->config, (const uint8_t *)data, length);
    return PORT_OK;
}

port_status_t port_crc_deinit(port_crc_handle_t *handle)
{
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != PORT_OK) {
        return PORT_NOT_READY;
    }
    avr_base_reset(&handle->base);
    return PORT_OK;
}

/* ---------------- RNG（软件 LCG） ---------------- */
port_status_t port_rng_init(port_rng_handle_t *handle)
{
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (handle->base.initialized) {
        return PORT_ALREADY_INIT;
    }
    s_rng_state ^= (port_tick_get() + 0x9E3779B9UL);
    if (s_rng_state == 0U) {
        s_rng_state = 0x12345678UL;
    }
    return avr_base_init(&handle->base, NULL);
}

port_status_t port_rng_deinit(port_rng_handle_t *handle)
{
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != PORT_OK) {
        return PORT_NOT_READY;
    }
    avr_base_reset(&handle->base);
    return PORT_OK;
}

port_status_t port_rng_get(port_rng_handle_t *handle, uint32_t *value)
{
    if (handle == NULL || value == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != PORT_OK) {
        return PORT_NOT_READY;
    }
    s_rng_state = s_rng_state * 1664525UL + 1013904223UL;
    *value = s_rng_state;
    return PORT_OK;
}

port_status_t port_rng_generate(port_rng_handle_t *handle, void *buffer, size_t length)
{
    uint8_t *output = (uint8_t *)buffer;
    size_t offset = 0U;

    if (handle == NULL || buffer == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != PORT_OK) {
        return PORT_NOT_READY;
    }
    while (offset < length) {
        uint32_t value;
        size_t chunk;

        s_rng_state = s_rng_state * 1664525UL + 1013904223UL;
        value = s_rng_state;
        chunk = PORT_MIN(sizeof(value), length - offset);
        memcpy(output + offset, &value, chunk);
        offset += chunk;
    }
    return PORT_OK;
}

/* ---------------- PWR ---------------- */
port_status_t port_pwr_init(void)
{
    return PORT_OK;
}

port_status_t port_pwr_enter(const port_pwr_config_t *config)
{
    if (config == NULL) {
        return PORT_INVALID_PARAM;
    }
    switch (config->mode) {
    case PORT_PWR_MODE_SLEEP:
        set_sleep_mode(SLEEP_MODE_IDLE);
        break;
    case PORT_PWR_MODE_STOP:
        set_sleep_mode(SLEEP_MODE_STANDBY);
        break;
    case PORT_PWR_MODE_STANDBY:
        set_sleep_mode(SLEEP_MODE_PWR_DOWN);
        break;
    default:
        return PORT_INVALID_PARAM;
    }
    sleep_enable();
    sleep_cpu();
    sleep_disable();
    return PORT_OK;
}

port_status_t port_pwr_get_status(port_pwr_status_t *status)
{
    if (status == NULL) {
        return PORT_INVALID_PARAM;
    }
    memset(status, 0, sizeof(*status));
    status->reset_flags = MCUSR;
    status->wakeup_flags = 0U;
    return PORT_OK;
}

port_status_t port_pwr_clear_flags(void)
{
    MCUSR = 0U;
    return PORT_OK;
}

/* ---------------- ADC ---------------- */
port_status_t port_adc_init(port_adc_handle_t *handle, const port_adc_config_t *config)
{
    if (handle == NULL || config == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (handle->base.initialized) {
        return PORT_ALREADY_INIT;
    }
    ADMUX = (uint8_t)(1U << REFS0);             /* AVcc 参考 */
    ADCSRA = (uint8_t)((1U << ADEN) | (1U << ADPS2) | (1U << ADPS1) | (1U << ADPS0));
    handle->config = *config;
    handle->dma = NULL;
    handle->callback = NULL;
    handle->user_data = NULL;
    return avr_base_init(&handle->base, NULL);
}

port_status_t port_adc_deinit(port_adc_handle_t *handle)
{
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != PORT_OK) {
        return PORT_NOT_READY;
    }
    ADCSRA = 0U;
    avr_base_reset(&handle->base);
    return PORT_OK;
}

port_status_t port_adc_config_channel(port_adc_handle_t *handle,
                                      const port_adc_channel_config_t *config)
{
    if (handle == NULL || config == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != PORT_OK) {
        return PORT_NOT_READY;
    }
    if (config->channel > 15U) {
        return PORT_INVALID_PARAM;
    }
    handle->base.priv = (void *)(uintptr_t)(config->channel + 1U);
    return PORT_OK;
}

port_status_t port_adc_start(port_adc_handle_t *handle)
{
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != PORT_OK) {
        return PORT_NOT_READY;
    }
    ADCSRA |= (uint8_t)(1U << ADEN);
    return PORT_OK;
}

port_status_t port_adc_stop(port_adc_handle_t *handle)
{
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != PORT_OK) {
        return PORT_NOT_READY;
    }
    ADCSRA &= (uint8_t)~(1U << ADEN);
    return PORT_OK;
}

port_status_t port_adc_read(port_adc_handle_t *handle, uint8_t channel,
                            uint32_t *value, uint32_t timeout)
{
    uint32_t start;
    uint32_t effective;

    if (handle == NULL || value == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != PORT_OK) {
        return PORT_NOT_READY;
    }
    if (channel > 15U) {
        return PORT_INVALID_PARAM;
    }
    ADMUX = (uint8_t)((ADMUX & 0xF0U) | (channel & 0x0FU));
    ADCSRA |= (uint8_t)(1U << ADSC);
    start = s_millis;
    effective = (timeout == 0U) ? 1U : timeout;
    while ((ADCSRA & (uint8_t)(1U << ADSC)) != 0U) {
        if (avr_timeout(start, effective)) {
            handle->base.error = PORT_TIMEOUT;
            return PORT_TIMEOUT;
        }
    }
    *value = (uint32_t)ADC;
    return PORT_OK;
}

port_status_t port_adc_start_it(port_adc_handle_t *handle)
{
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != PORT_OK) {
        return PORT_NOT_READY;
    }
    ADCSRA |= (uint8_t)((1U << ADIE) | (1U << ADEN));
    return PORT_OK;
}

port_status_t port_adc_start_dma(port_adc_handle_t *handle, uint32_t *buffer,
                                 size_t length)
{
    PORT_UNUSED(buffer);
    PORT_UNUSED(length);
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != PORT_OK) {
        return PORT_NOT_READY;
    }
    return PORT_NOT_SUPPORTED;
}

port_status_t port_adc_calibrate(port_adc_handle_t *handle)
{
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != PORT_OK) {
        return PORT_NOT_READY;
    }
    /* AVR ADC 无硬件校准，首次转换会自动完成采样保持初始化。 */
    return PORT_OK;
}

uint32_t port_adc_get_max_value(port_adc_handle_t *handle)
{
    if (handle == NULL || !handle->base.initialized) {
        return 0U;
    }
    return 1023U;
}

/* ---------------- 不支持的外设（仅满足链接） ---------------- */
port_status_t port_dma_init(port_dma_handle_t *handle, const port_dma_config_t *config)
{
    PORT_UNUSED(config);
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    return PORT_NOT_SUPPORTED;
}

port_status_t port_dma_deinit(port_dma_handle_t *handle)
{
    PORT_UNUSED(handle);
    return PORT_NOT_SUPPORTED;
}

port_status_t port_dma_start(port_dma_handle_t *handle, uintptr_t src,
                             uintptr_t dst, size_t length)
{
    PORT_UNUSED(handle);
    PORT_UNUSED(src);
    PORT_UNUSED(dst);
    PORT_UNUSED(length);
    return PORT_NOT_SUPPORTED;
}

port_status_t port_dma_stop(port_dma_handle_t *handle)
{
    PORT_UNUSED(handle);
    return PORT_NOT_SUPPORTED;
}

port_status_t port_dma_suspend(port_dma_handle_t *handle)
{
    PORT_UNUSED(handle);
    return PORT_NOT_SUPPORTED;
}

port_status_t port_dma_resume(port_dma_handle_t *handle)
{
    PORT_UNUSED(handle);
    return PORT_NOT_SUPPORTED;
}

size_t port_dma_get_remaining(port_dma_handle_t *handle)
{
    PORT_UNUSED(handle);
    return 0U;
}

port_status_t port_i2s_init(port_i2s_handle_t *handle, const port_i2s_config_t *config)
{
    PORT_UNUSED(handle);
    PORT_UNUSED(config);
    return PORT_NOT_SUPPORTED;
}

port_status_t port_i2s_deinit(port_i2s_handle_t *handle)
{
    PORT_UNUSED(handle);
    return PORT_NOT_SUPPORTED;
}

port_status_t port_i2s_transmit(port_i2s_handle_t *handle, const void *data,
                                size_t length, uint32_t timeout)
{
    PORT_UNUSED(handle);
    PORT_UNUSED(data);
    PORT_UNUSED(length);
    PORT_UNUSED(timeout);
    return PORT_NOT_SUPPORTED;
}

port_status_t port_i2s_receive(port_i2s_handle_t *handle, void *data,
                               size_t length, uint32_t timeout)
{
    PORT_UNUSED(handle);
    PORT_UNUSED(data);
    PORT_UNUSED(length);
    PORT_UNUSED(timeout);
    return PORT_NOT_SUPPORTED;
}

port_status_t port_i2s_transmit_dma(port_i2s_handle_t *handle, const void *data,
                                    size_t length)
{
    PORT_UNUSED(handle);
    PORT_UNUSED(data);
    PORT_UNUSED(length);
    return PORT_NOT_SUPPORTED;
}

port_status_t port_i2s_receive_dma(port_i2s_handle_t *handle, void *data,
                                   size_t length)
{
    PORT_UNUSED(handle);
    PORT_UNUSED(data);
    PORT_UNUSED(length);
    return PORT_NOT_SUPPORTED;
}

port_status_t port_i2s_abort(port_i2s_handle_t *handle)
{
    PORT_UNUSED(handle);
    return PORT_NOT_SUPPORTED;
}

port_status_t port_can_init(port_can_handle_t *handle, const port_can_config_t *config)
{
    PORT_UNUSED(handle);
    PORT_UNUSED(config);
    return PORT_NOT_SUPPORTED;
}

port_status_t port_can_deinit(port_can_handle_t *handle)
{
    PORT_UNUSED(handle);
    return PORT_NOT_SUPPORTED;
}

port_status_t port_can_start(port_can_handle_t *handle)
{
    PORT_UNUSED(handle);
    return PORT_NOT_SUPPORTED;
}

port_status_t port_can_stop(port_can_handle_t *handle)
{
    PORT_UNUSED(handle);
    return PORT_NOT_SUPPORTED;
}

port_status_t port_can_transmit(port_can_handle_t *handle, const port_can_frame_t *frame,
                                uint32_t timeout)
{
    PORT_UNUSED(handle);
    PORT_UNUSED(frame);
    PORT_UNUSED(timeout);
    return PORT_NOT_SUPPORTED;
}

port_status_t port_can_receive(port_can_handle_t *handle, port_can_frame_t *frame,
                               uint32_t timeout)
{
    PORT_UNUSED(handle);
    PORT_UNUSED(frame);
    PORT_UNUSED(timeout);
    return PORT_NOT_SUPPORTED;
}

port_status_t port_can_transmit_it(port_can_handle_t *handle,
                                   const port_can_frame_t *frame)
{
    PORT_UNUSED(handle);
    PORT_UNUSED(frame);
    return PORT_NOT_SUPPORTED;
}

port_status_t port_can_add_filter(port_can_handle_t *handle,
                                  const port_can_filter_t *filter)
{
    PORT_UNUSED(handle);
    PORT_UNUSED(filter);
    return PORT_NOT_SUPPORTED;
}

port_status_t port_can_clear_filters(port_can_handle_t *handle)
{
    PORT_UNUSED(handle);
    return PORT_NOT_SUPPORTED;
}

port_status_t port_dac_init(port_dac_handle_t *handle, const port_dac_config_t *config)
{
    PORT_UNUSED(handle);
    PORT_UNUSED(config);
    return PORT_NOT_SUPPORTED;
}

port_status_t port_dac_deinit(port_dac_handle_t *handle)
{
    PORT_UNUSED(handle);
    return PORT_NOT_SUPPORTED;
}

port_status_t port_dac_start(port_dac_handle_t *handle)
{
    PORT_UNUSED(handle);
    return PORT_NOT_SUPPORTED;
}

port_status_t port_dac_stop(port_dac_handle_t *handle)
{
    PORT_UNUSED(handle);
    return PORT_NOT_SUPPORTED;
}

port_status_t port_dac_write(port_dac_handle_t *handle, uint32_t value)
{
    PORT_UNUSED(handle);
    PORT_UNUSED(value);
    return PORT_NOT_SUPPORTED;
}

port_status_t port_dac_write_dma(port_dac_handle_t *handle, const uint32_t *buffer,
                                 size_t length)
{
    PORT_UNUSED(handle);
    PORT_UNUSED(buffer);
    PORT_UNUSED(length);
    return PORT_NOT_SUPPORTED;
}

port_status_t port_comp_init(port_comp_handle_t *handle, const port_comp_config_t *config)
{
    PORT_UNUSED(handle);
    PORT_UNUSED(config);
    return PORT_NOT_SUPPORTED;
}

port_status_t port_comp_start(port_comp_handle_t *handle)
{
    PORT_UNUSED(handle);
    return PORT_NOT_SUPPORTED;
}

port_status_t port_comp_stop(port_comp_handle_t *handle)
{
    PORT_UNUSED(handle);
    return PORT_NOT_SUPPORTED;
}

port_status_t port_comp_read(port_comp_handle_t *handle, bool *value)
{
    PORT_UNUSED(handle);
    PORT_UNUSED(value);
    return PORT_NOT_SUPPORTED;
}

port_status_t port_opamp_init(port_opamp_handle_t *handle,
                              const port_opamp_config_t *config)
{
    PORT_UNUSED(handle);
    PORT_UNUSED(config);
    return PORT_NOT_SUPPORTED;
}

port_status_t port_opamp_start(port_opamp_handle_t *handle)
{
    PORT_UNUSED(handle);
    return PORT_NOT_SUPPORTED;
}

port_status_t port_opamp_stop(port_opamp_handle_t *handle)
{
    PORT_UNUSED(handle);
    return PORT_NOT_SUPPORTED;
}

port_status_t port_qspi_init(port_qspi_handle_t *handle, const port_qspi_config_t *config)
{
    PORT_UNUSED(handle);
    PORT_UNUSED(config);
    return PORT_NOT_SUPPORTED;
}

port_status_t port_qspi_deinit(port_qspi_handle_t *handle)
{
    PORT_UNUSED(handle);
    return PORT_NOT_SUPPORTED;
}

port_status_t port_qspi_command(port_qspi_handle_t *handle,
                                const port_qspi_command_t *command)
{
    PORT_UNUSED(handle);
    PORT_UNUSED(command);
    return PORT_NOT_SUPPORTED;
}

port_status_t port_qspi_transmit(port_qspi_handle_t *handle, const void *data,
                                 size_t length, uint32_t timeout)
{
    PORT_UNUSED(handle);
    PORT_UNUSED(data);
    PORT_UNUSED(length);
    PORT_UNUSED(timeout);
    return PORT_NOT_SUPPORTED;
}

port_status_t port_qspi_receive(port_qspi_handle_t *handle, void *data,
                                size_t length, uint32_t timeout)
{
    PORT_UNUSED(handle);
    PORT_UNUSED(data);
    PORT_UNUSED(length);
    PORT_UNUSED(timeout);
    return PORT_NOT_SUPPORTED;
}

port_status_t port_qspi_memory_map(port_qspi_handle_t *handle)
{
    PORT_UNUSED(handle);
    return PORT_NOT_SUPPORTED;
}

port_status_t port_qspi_memory_unmap(port_qspi_handle_t *handle)
{
    PORT_UNUSED(handle);
    return PORT_NOT_SUPPORTED;
}

port_status_t port_sdio_init(port_sdio_handle_t *handle, const port_sdio_config_t *config)
{
    PORT_UNUSED(handle);
    PORT_UNUSED(config);
    return PORT_NOT_SUPPORTED;
}

port_status_t port_sdio_deinit(port_sdio_handle_t *handle)
{
    PORT_UNUSED(handle);
    return PORT_NOT_SUPPORTED;
}

port_status_t port_sdio_read(port_sdio_handle_t *handle, uint32_t block,
                             void *buffer, size_t block_count, uint32_t timeout)
{
    PORT_UNUSED(handle);
    PORT_UNUSED(block);
    PORT_UNUSED(buffer);
    PORT_UNUSED(block_count);
    PORT_UNUSED(timeout);
    return PORT_NOT_SUPPORTED;
}

port_status_t port_sdio_write(port_sdio_handle_t *handle, uint32_t block,
                              const void *buffer, size_t block_count, uint32_t timeout)
{
    PORT_UNUSED(handle);
    PORT_UNUSED(block);
    PORT_UNUSED(buffer);
    PORT_UNUSED(block_count);
    PORT_UNUSED(timeout);
    return PORT_NOT_SUPPORTED;
}

port_status_t port_sdio_read_dma(port_sdio_handle_t *handle, uint32_t block,
                                 void *buffer, size_t block_count)
{
    PORT_UNUSED(handle);
    PORT_UNUSED(block);
    PORT_UNUSED(buffer);
    PORT_UNUSED(block_count);
    return PORT_NOT_SUPPORTED;
}

port_status_t port_sdio_write_dma(port_sdio_handle_t *handle, uint32_t block,
                                  const void *buffer, size_t block_count)
{
    PORT_UNUSED(handle);
    PORT_UNUSED(block);
    PORT_UNUSED(buffer);
    PORT_UNUSED(block_count);
    return PORT_NOT_SUPPORTED;
}

port_status_t port_usb_init(port_usb_handle_t *handle, const port_usb_config_t *config)
{
    PORT_UNUSED(handle);
    PORT_UNUSED(config);
    return PORT_NOT_SUPPORTED;
}

port_status_t port_usb_deinit(port_usb_handle_t *handle)
{
    PORT_UNUSED(handle);
    return PORT_NOT_SUPPORTED;
}

port_status_t port_usb_start(port_usb_handle_t *handle)
{
    PORT_UNUSED(handle);
    return PORT_NOT_SUPPORTED;
}

port_status_t port_usb_stop(port_usb_handle_t *handle)
{
    PORT_UNUSED(handle);
    return PORT_NOT_SUPPORTED;
}

port_status_t port_usb_connect(port_usb_handle_t *handle)
{
    PORT_UNUSED(handle);
    return PORT_NOT_SUPPORTED;
}

port_status_t port_usb_disconnect(port_usb_handle_t *handle)
{
    PORT_UNUSED(handle);
    return PORT_NOT_SUPPORTED;
}

port_status_t port_eth_init(port_eth_handle_t *handle, const port_eth_config_t *config)
{
    PORT_UNUSED(handle);
    PORT_UNUSED(config);
    return PORT_NOT_SUPPORTED;
}

port_status_t port_eth_deinit(port_eth_handle_t *handle)
{
    PORT_UNUSED(handle);
    return PORT_NOT_SUPPORTED;
}

port_status_t port_eth_start(port_eth_handle_t *handle)
{
    PORT_UNUSED(handle);
    return PORT_NOT_SUPPORTED;
}

port_status_t port_eth_stop(port_eth_handle_t *handle)
{
    PORT_UNUSED(handle);
    return PORT_NOT_SUPPORTED;
}

port_status_t port_eth_transmit(port_eth_handle_t *handle, const void *data,
                                size_t length)
{
    PORT_UNUSED(handle);
    PORT_UNUSED(data);
    PORT_UNUSED(length);
    return PORT_NOT_SUPPORTED;
}

port_status_t port_eth_receive(port_eth_handle_t *handle, void *buffer,
                               size_t buffer_size, size_t *received_length)
{
    PORT_UNUSED(handle);
    PORT_UNUSED(buffer);
    PORT_UNUSED(buffer_size);
    PORT_UNUSED(received_length);
    return PORT_NOT_SUPPORTED;
}

/* ---------------- 能力查询 ---------------- */
uint32_t port_get_capabilities(port_device_type_t type, uint8_t instance)
{
    PORT_UNUSED(instance);
    if (type == PORT_DEVICE_FLASH) {
        return PORT_FLASH_FLAG_READ;
    }
    /* AVR 平台不支持 DMA/IT 外设传输，统一返回 0。 */
    return 0U;
}

/* ---------------- 平台注册 ---------------- */
static bool s_platform_ready = false;

static const port_platform_ops_t s_avr_platform_ops = {
    "avr",
    PORT_MCU_AVR,
    avr_platform_init,
    avr_platform_deinit,
    avr_platform_get_info
};

static const port_os_ops_t s_avr_os_ops = {
    avr_os_mutex_create,
    avr_os_mutex_delete,
    avr_os_mutex_lock,
    avr_os_mutex_unlock,
    avr_os_sem_create,
    avr_os_sem_wait,
    avr_os_sem_post,
    avr_os_tick_get,
    avr_os_delay_ms,
    avr_os_critical_enter,
    avr_os_critical_exit
};

port_status_t port_platform_install(void)
{
    port_status_t status;

    if (s_platform_ready) {
        return PORT_OK;
    }
    status = port_platform_register(&s_avr_platform_ops);
    if (status != PORT_OK) {
        return status;
    }
    status = port_os_register(&s_avr_os_ops);
    if (status != PORT_OK) {
        return status;
    }
    status = avr_platform_init();
    if (status == PORT_OK) {
        s_platform_ready = true;
    }
    return status;
}


