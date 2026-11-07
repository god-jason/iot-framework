/*
 * AVR 平台实现（新 PORT 硬件抽象层），寄存器级，不依赖 HAL。
 *
 * 目标：ATmega328P @ 16 MHz（Arduino UNO 形态）。
 * GPIO 编码约定：IOT_PIN(port, pin) 中 port 0 -> PORTB，1 -> PORTC，
 * 2 -> PORTD；pin 为 0..7。逻辑端口号沿用了 STM32 风格的连续编号。
 *
 * 中断向量：本文件定义了 Timer0 溢出（毫秒时基）、Timer0/1/2 比较、
 * Timer1 输入捕获与 EXTI 分发所需的 ISR。若应用需要自定义这些向量，
 * 应关闭对应外设或修改本文件。
 */
#include "iot.h"

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
volatile uint32_t g_avr_millis = 0U;

ISR(TIMER0_OVF_vect)
{
    g_avr_millis++;
}

/* ---------------- GPIO 寄存器表 ---------------- */
static volatile uint8_t *const s_ddr[AVR_GPIO_PORT_COUNT] = { &DDRB, &DDRC, &DDRD };
static volatile uint8_t *const s_port_reg[AVR_GPIO_PORT_COUNT] = { &PORTB, &PORTC, &PORTD };
static volatile uint8_t *const s_pin_reg[AVR_GPIO_PORT_COUNT] = { &PINB, &PINC, &PIND };

/* ---------------- EXTI 状态 ---------------- */
typedef struct {
    iot_exti_callback_t callback;
    void *user_data;
    iot_pin_t pin;
    iot_exti_trigger_t trigger;
    bool enabled;
} avr_exti_entry_t;

static avr_exti_entry_t s_exti[AVR_GPIO_PORT_COUNT][8];
static uint8_t s_pcint_last[AVR_GPIO_PORT_COUNT];

/* ---------------- 定时器占用表 ---------------- */
static iot_tim_t *s_tim_owner[3] = { NULL, NULL, NULL };
static iot_pwm_t *s_pwm_owner[3] = { NULL, NULL, NULL };

static iot_capture_t *s_capture_owner = NULL;
static volatile uint16_t s_capture_value = 0U;

/* ---------------- 软件 RTC ---------------- */
static iot_rtc_time_t s_rtc_base;
static uint32_t s_rtc_base_tick = 0U;
static bool s_rtc_valid = false;

/* ---------------- 软件 RNG ---------------- */
static uint32_t s_rng_state = 0x12345678UL;

/* ---------------- 通用辅助 ---------------- */
static bool avr_pin_valid(iot_pin_t pin)
{
    return (IOT_PIN_GET_PORT(pin) < AVR_GPIO_PORT_COUNT) &&
           (IOT_PIN_GET_INDEX(pin) < 8U);
}

static iot_status_t avr_base_init(iot_handle_base_t *base, void *instance)
{
    if (base->state != IOT_STATE_RESET) {
        return IOT_ALREADY_INIT;
    }
    base->instance = instance;
    base->state = IOT_STATE_READY;
    base->error = IOT_OK;
    return IOT_OK;
}

static iot_status_t avr_base_check(const iot_handle_base_t *base)
{
    return base->state != IOT_STATE_RESET ? IOT_OK : IOT_NOT_READY;
}

static void avr_base_reset(iot_handle_base_t *base)
{
    base->instance = NULL;
    base->state = IOT_STATE_RESET;
    base->error = IOT_ERROR;
}

static bool avr_timeout(uint32_t start, uint32_t timeout)
{
    if (timeout == IOT_TIMEOUT_FOREVER) {
        return false;
    }
    return (uint32_t)(g_avr_millis - start) >= timeout;
}

/* ---------------- Platform ---------------- */
iot_status_t iot_init(void)
{
    /* Timer0：clk/64，溢出周期 1.024 ms（16 MHz），作为毫秒时基。 */
    TCCR0A = 0U;
    TCCR0B = (uint8_t)((1U << CS01) | (1U << CS00));
    TIMSK0 |= (uint8_t)(1U << TOIE0);
    s_rng_state = 0x12345678UL ^ (uint32_t)g_avr_millis;
    return IOT_OK;
}

iot_status_t iot_deinit(void)
{
    TIMSK0 &= (uint8_t)~(1U << TOIE0);
    return IOT_OK;
}

iot_status_t iot_get_info(iot_mcu_info_t *info)
{
    if (info == NULL) {
        return IOT_INVALID_PARAM;
    }
    memset(info, 0, sizeof(*info));
    info->family = IOT_MCU_AVR;
    info->model = AVR_DEVICE_MODEL;
    info->revision = 0U;
    info->core_clock = (uint32_t)F_CPU;
    info->ahb_clock = (uint32_t)F_CPU;
    info->apb1_clock = (uint32_t)F_CPU;
    info->apb2_clock = (uint32_t)F_CPU;
    info->flash_size = AVR_FLASH_SIZE;
    info->sram_size = AVR_SRAM_SIZE;
    return IOT_OK;
}

/* ---------------- 时钟 ---------------- */
iot_status_t iot_clock_init(const iot_clock_config_t *config)
{
    if (config == NULL) {
        return IOT_INVALID_PARAM;
    }
    /* ATmega328P 时钟由熔丝位/CKDIV 决定，运行时不可重配。 */
    return IOT_OK;
}

uint32_t iot_clock_get_frequency(iot_clock_bus_t bus)
{
    IOT_UNUSED(bus);
    return (uint32_t)F_CPU;
}

iot_status_t iot_clock_enable(void *peripheral)
{
    return (peripheral == NULL) ? IOT_INVALID_PARAM : IOT_OK;
}

iot_status_t iot_clock_disable(void *peripheral)
{
    return (peripheral == NULL) ? IOT_INVALID_PARAM : IOT_OK;
}

/* ---------------- 引脚复用 ---------------- */
iot_status_t iot_pin_init(const iot_pin_config_t *config)
{
    uint8_t port;
    uint8_t bit;

    if (config == NULL || !avr_pin_valid(config->pin)) {
        return IOT_INVALID_PARAM;
    }
    if (config->function == IOT_PIN_FUNCTION_AF) {
        /* AVR 复用功能固定在特定引脚，不支持任意映射。 */
        return IOT_NOT_SUPPORTED;
    }
    port = (uint8_t)IOT_PIN_GET_PORT(config->pin);
    bit = (uint8_t)IOT_PIN_GET_INDEX(config->pin);
    *s_ddr[port] &= (uint8_t)~(1U << bit);
    *s_port_reg[port] &= (uint8_t)~(1U << bit);
    return IOT_OK;
}

iot_status_t iot_pin_deinit(iot_pin_t pin)
{
    uint8_t port;
    uint8_t bit;

    if (!avr_pin_valid(pin)) {
        return IOT_INVALID_PARAM;
    }
    port = (uint8_t)IOT_PIN_GET_PORT(pin);
    bit = (uint8_t)IOT_PIN_GET_INDEX(pin);
    *s_ddr[port] &= (uint8_t)~(1U << bit);
    *s_port_reg[port] &= (uint8_t)~(1U << bit);
    return IOT_OK;
}

/* ---------------- GPIO ---------------- */
iot_status_t iot_gpio_init(iot_pin_t pin, const iot_gpio_config_t *config)
{
    uint8_t port;
    uint8_t bit;

    if (config == NULL || !avr_pin_valid(pin)) {
        return IOT_INVALID_PARAM;
    }
    port = (uint8_t)IOT_PIN_GET_PORT(pin);
    bit = (uint8_t)IOT_PIN_GET_INDEX(pin);

    switch (config->mode) {
    case IOT_GPIO_MODE_OUTPUT:
        *s_ddr[port] |= (uint8_t)(1U << bit);
        if (config->initial_level == IOT_GPIO_HIGH) {
            *s_port_reg[port] |= (uint8_t)(1U << bit);
        } else {
            *s_port_reg[port] &= (uint8_t)~(1U << bit);
        }
        break;
    case IOT_GPIO_MODE_INPUT:
        *s_ddr[port] &= (uint8_t)~(1U << bit);
        if (config->pull == IOT_GPIO_PULLUP) {
            *s_port_reg[port] |= (uint8_t)(1U << bit);
        } else {
            *s_port_reg[port] &= (uint8_t)~(1U << bit);
        }
        break;
    case IOT_GPIO_MODE_ANALOG:
        *s_ddr[port] &= (uint8_t)~(1U << bit);
        *s_port_reg[port] &= (uint8_t)~(1U << bit);
        break;
    case IOT_GPIO_MODE_AF:
    default:
        return IOT_NOT_SUPPORTED;
    }
    return IOT_OK;
}

iot_status_t iot_gpio_deinit(iot_pin_t pin)
{
    return iot_pin_deinit(pin);
}

iot_status_t iot_gpio_write(iot_pin_t pin, iot_gpio_level_t level)
{
    uint8_t port;
    uint8_t bit;

    if (!avr_pin_valid(pin)) {
        return IOT_INVALID_PARAM;
    }
    port = (uint8_t)IOT_PIN_GET_PORT(pin);
    bit = (uint8_t)IOT_PIN_GET_INDEX(pin);
    if (level == IOT_GPIO_HIGH) {
        *s_port_reg[port] |= (uint8_t)(1U << bit);
    } else {
        *s_port_reg[port] &= (uint8_t)~(1U << bit);
    }
    return IOT_OK;
}

iot_status_t iot_gpio_read(iot_pin_t pin, iot_gpio_level_t *level)
{
    uint8_t port;
    uint8_t bit;

    if (level == NULL || !avr_pin_valid(pin)) {
        return IOT_INVALID_PARAM;
    }
    port = (uint8_t)IOT_PIN_GET_PORT(pin);
    bit = (uint8_t)IOT_PIN_GET_INDEX(pin);
    *level = ((*s_pin_reg[port] & (uint8_t)(1U << bit)) != 0U) ? IOT_GPIO_HIGH
                                                               : IOT_GPIO_LOW;
    return IOT_OK;
}

iot_status_t iot_gpio_toggle(iot_pin_t pin)
{
    uint8_t port;
    uint8_t bit;

    if (!avr_pin_valid(pin)) {
        return IOT_INVALID_PARAM;
    }
    port = (uint8_t)IOT_PIN_GET_PORT(pin);
    bit = (uint8_t)IOT_PIN_GET_INDEX(pin);
    *s_pin_reg[port] = (uint8_t)(1U << bit);   /* 写 PINx 翻转 */
    return IOT_OK;
}

iot_status_t iot_gpio_set(iot_pin_t pin)
{
    return iot_gpio_write(pin, IOT_GPIO_HIGH);
}

iot_status_t iot_gpio_reset(iot_pin_t pin)
{
    return iot_gpio_write(pin, IOT_GPIO_LOW);
}

/* ---------------- EXTI ---------------- */
static void avr_exti_apply_int(uint8_t line, iot_exti_trigger_t trigger)
{
    uint8_t isc;

    switch (trigger) {
    case IOT_EXTI_RISING:  isc = 0x03U; break;
    case IOT_EXTI_FALLING: isc = 0x02U; break;
    case IOT_EXTI_BOTH:    isc = 0x01U; break;
    default:                isc = 0x00U; break;
    }
    if (line == 0U) {
        EICRA = (uint8_t)((EICRA & 0xFCU) | isc);
    } else {
        EICRA = (uint8_t)((EICRA & 0xF3U) | (uint8_t)(isc << 2));
    }
}

iot_status_t iot_exti_init(iot_pin_t pin, const iot_exti_config_t *config)
{
    uint8_t port;
    uint8_t bit;
    uint8_t group;

    if (config == NULL || !avr_pin_valid(pin)) {
        return IOT_INVALID_PARAM;
    }
    port = (uint8_t)IOT_PIN_GET_PORT(pin);
    bit = (uint8_t)IOT_PIN_GET_INDEX(pin);

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
    return IOT_OK;
}

iot_status_t iot_exti_enable(iot_pin_t pin)
{
    uint8_t port;
    uint8_t bit;

    if (!avr_pin_valid(pin)) {
        return IOT_INVALID_PARAM;
    }
    port = (uint8_t)IOT_PIN_GET_PORT(pin);
    bit = (uint8_t)IOT_PIN_GET_INDEX(pin);
    if (!s_exti[port][bit].enabled) {
        return IOT_NOT_READY;
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
    return IOT_OK;
}

iot_status_t iot_exti_disable(iot_pin_t pin)
{
    uint8_t port;
    uint8_t bit;

    if (!avr_pin_valid(pin)) {
        return IOT_INVALID_PARAM;
    }
    port = (uint8_t)IOT_PIN_GET_PORT(pin);
    bit = (uint8_t)IOT_PIN_GET_INDEX(pin);
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
    return IOT_OK;
}

iot_status_t iot_exti_clear(iot_pin_t pin)
{
    uint8_t port;
    uint8_t bit;

    if (!avr_pin_valid(pin)) {
        return IOT_INVALID_PARAM;
    }
    port = (uint8_t)IOT_PIN_GET_PORT(pin);
    bit = (uint8_t)IOT_PIN_GET_INDEX(pin);
    if (port == 2U && bit == 2U) {
        EIFR = (uint8_t)(1U << INTF0);
    } else if (port == 2U && bit == 3U) {
        EIFR = (uint8_t)(1U << INTF1);
    } else {
        PCIFR = (uint8_t)(1U << port);
    }
    return IOT_OK;
}

iot_status_t iot_exti_deinit(iot_pin_t pin)
{
    uint8_t port;
    uint8_t bit;

    if (!avr_pin_valid(pin)) {
        return IOT_INVALID_PARAM;
    }
    port = (uint8_t)IOT_PIN_GET_PORT(pin);
    bit = (uint8_t)IOT_PIN_GET_INDEX(pin);
    (void)iot_exti_disable(pin);
    s_exti[port][bit].callback = NULL;
    s_exti[port][bit].user_data = NULL;
    s_exti[port][bit].enabled = false;
    return IOT_OK;
}

/*
 * vector: 0=INT0, 1=INT1, 2=PCINT0, 3=PCINT1, 4=PCINT2。
 * 由应用 ISR 调用，例如：
 *   ISR(INT0_vect) { iot_avr_exti_irq(0); }
 *   ISR(PCINT0_vect) { iot_avr_exti_irq(2); }
 */
void iot_avr_exti_irq(uint8_t vector)
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
iot_status_t iot_uart_init(iot_uart_t *handle, const iot_uart_config_t *config)
{
    uint32_t ubrr;
    uint8_t ucsrc = 0U;
    uint8_t ucsrb = 0U;

    if (handle == NULL || config == NULL || config->baudrate == 0U) {
        return IOT_INVALID_PARAM;
    }
    if (handle->base.state != IOT_STATE_RESET) {
        return IOT_ALREADY_INIT;
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
        return IOT_INVALID_PARAM;
    }
    UBRR0H = (uint8_t)(ubrr >> 8);
    UBRR0L = (uint8_t)(ubrr & 0xFFU);

    switch (config->word_length) {
    case IOT_UART_WORD_7BIT:
        ucsrc |= (uint8_t)(1U << UCSZ01);
        break;
    case IOT_UART_WORD_9BIT:
        ucsrc |= (uint8_t)((1U << UCSZ01) | (1U << UCSZ00));
        ucsrb |= (uint8_t)(1U << UCSZ02);
        break;
    default:
        ucsrc |= (uint8_t)((1U << UCSZ01) | (1U << UCSZ00));
        break;
    }
    if (config->parity == IOT_UART_PARITY_EVEN) {
        ucsrc |= (uint8_t)(1U << UPM01);
    } else if (config->parity == IOT_UART_PARITY_ODD) {
        ucsrc |= (uint8_t)((1U << UPM01) | (1U << UPM00));
    }
    if (config->stop_bits == IOT_UART_STOP_2) {
        ucsrc |= (uint8_t)(1U << USBS0);
    }
    if (config->mode == IOT_UART_MODE_TX || config->mode == IOT_UART_MODE_TX_RX) {
        ucsrb |= (uint8_t)(1U << TXEN0);
    }
    if (config->mode == IOT_UART_MODE_RX || config->mode == IOT_UART_MODE_TX_RX) {
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

iot_status_t iot_uart_deinit(iot_uart_t *handle)
{
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != IOT_OK) {
        return IOT_NOT_READY;
    }
    UCSR0B = 0U;
    avr_base_reset(&handle->base);
    return IOT_OK;
}

iot_status_t iot_uart_transmit(iot_uart_t *handle, const uint8_t *data,
                                 size_t length, uint32_t timeout)
{
    size_t i;
    uint32_t start;

    if (handle == NULL || data == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != IOT_OK) {
        return IOT_NOT_READY;
    }
    start = g_avr_millis;
    for (i = 0U; i < length; i++) {
        while ((UCSR0A & (uint8_t)(1U << UDRE0)) == 0U) {
            if (avr_timeout(start, (timeout == 0U) ? 1U : timeout)) {
                handle->base.error = IOT_TIMEOUT;
                return IOT_TIMEOUT;
            }
        }
        UDR0 = data[i];
    }
    return IOT_OK;
}

iot_status_t iot_uart_receive(iot_uart_t *handle, uint8_t *data,
                                size_t length, uint32_t timeout)
{
    size_t i;
    uint32_t start;

    if (handle == NULL || data == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != IOT_OK) {
        return IOT_NOT_READY;
    }
    start = g_avr_millis;
    for (i = 0U; i < length; i++) {
        while ((UCSR0A & (uint8_t)(1U << RXC0)) == 0U) {
            if (avr_timeout(start, (timeout == 0U) ? 1U : timeout)) {
                handle->base.error = IOT_TIMEOUT;
                return IOT_TIMEOUT;
            }
        }
        data[i] = UDR0;
    }
    return IOT_OK;
}

iot_status_t iot_uart_transmit_it(iot_uart_t *handle, const uint8_t *data,
                                    size_t length)
{
    IOT_UNUSED(data);
    IOT_UNUSED(length);
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != IOT_OK) {
        return IOT_NOT_READY;
    }
    return IOT_NOT_SUPPORTED;
}

iot_status_t iot_uart_receive_it(iot_uart_t *handle, uint8_t *data,
                                   size_t length)
{
    IOT_UNUSED(data);
    IOT_UNUSED(length);
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != IOT_OK) {
        return IOT_NOT_READY;
    }
    return IOT_NOT_SUPPORTED;
}

iot_status_t iot_uart_transmit_dma(iot_uart_t *handle, const uint8_t *data,
                                     size_t length)
{
    IOT_UNUSED(data);
    IOT_UNUSED(length);
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != IOT_OK) {
        return IOT_NOT_READY;
    }
    return IOT_NOT_SUPPORTED;
}

iot_status_t iot_uart_receive_dma(iot_uart_t *handle, uint8_t *data,
                                    size_t length)
{
    IOT_UNUSED(data);
    IOT_UNUSED(length);
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != IOT_OK) {
        return IOT_NOT_READY;
    }
    return IOT_NOT_SUPPORTED;
}

iot_status_t iot_uart_receive_to_idle(iot_uart_t *handle, uint8_t *data,
                                        size_t length, iot_transfer_mode_t mode)
{
    IOT_UNUSED(data);
    IOT_UNUSED(length);
    IOT_UNUSED(mode);
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != IOT_OK) {
        return IOT_NOT_READY;
    }
    return IOT_NOT_SUPPORTED;
}

size_t iot_uart_get_rx_count(iot_uart_t *handle)
{
    if (handle == NULL || handle->base.state == IOT_STATE_RESET) {
        return 0U;
    }
    return ((UCSR0A & (uint8_t)(1U << RXC0)) != 0U) ? 1U : 0U;
}

iot_status_t iot_uart_abort_tx(iot_uart_t *handle)
{
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != IOT_OK) {
        return IOT_NOT_READY;
    }
    handle->base.state = IOT_STATE_READY;
    return IOT_OK;
}

iot_status_t iot_uart_abort_rx(iot_uart_t *handle)
{
    return iot_uart_abort_tx(handle);
}

iot_status_t iot_uart_abort(iot_uart_t *handle)
{
    return iot_uart_abort_tx(handle);
}

iot_state_t iot_uart_get_state(iot_uart_t *handle)
{
    return (handle != NULL) ? handle->base.state : IOT_STATE_RESET;
}

iot_status_t iot_uart_get_error(iot_uart_t *handle)
{
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    return handle->base.error;
}

iot_status_t iot_uart_register_callback(iot_uart_t *handle,
                                          iot_uart_callback_t callback, void *user_data)
{
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != IOT_OK) {
        return IOT_NOT_READY;
    }
    handle->callback = callback;
    handle->user_data = user_data;
    return IOT_OK;
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

static iot_status_t avr_spi_wait(uint32_t start, uint32_t timeout)
{
    while ((SPSR & (uint8_t)(1U << SPIF)) == 0U) {
        if (avr_timeout(start, (timeout == 0U) ? 1U : timeout)) {
            return IOT_TIMEOUT;
        }
    }
    return IOT_OK;
}

iot_status_t iot_spi_init(iot_spi_t *handle, const iot_spi_config_t *config)
{
    uint8_t spcr = (uint8_t)(1U << SPE);
    bool double_speed = false;
    uint8_t spr;

    if (handle == NULL || config == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (config->data_bits != 8U) {
        return IOT_NOT_SUPPORTED;
    }
    if (handle->base.state != IOT_STATE_RESET) {
        return IOT_ALREADY_INIT;
    }
    if (config->mode == IOT_SPI_MODE_MASTER) {
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
    if (config->cpol == IOT_SPI_CPOL_HIGH) {
        spcr |= (uint8_t)(1U << CPOL);
    }
    if (config->cpha == IOT_SPI_CPHA_2EDGE) {
        spcr |= (uint8_t)(1U << CPHA);
    }
    if (config->bit_order == IOT_SPI_BITORDER_LSB) {
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

iot_status_t iot_spi_deinit(iot_spi_t *handle)
{
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != IOT_OK) {
        return IOT_NOT_READY;
    }
    SPCR &= (uint8_t)~(1U << SPE);
    avr_base_reset(&handle->base);
    return IOT_OK;
}

iot_status_t iot_spi_transmit(iot_spi_t *handle, const void *data,
                                size_t length, uint32_t timeout)
{
    const uint8_t *source = (const uint8_t *)data;
    size_t i;
    uint32_t start;
    iot_status_t status;

    if (handle == NULL || data == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != IOT_OK) {
        return IOT_NOT_READY;
    }
    start = g_avr_millis;
    for (i = 0U; i < length; i++) {
        SPDR = source[i];
        status = avr_spi_wait(start, timeout);
        if (status != IOT_OK) {
            return status;
        }
        (void)SPSR;
        (void)SPDR;
    }
    return IOT_OK;
}

iot_status_t iot_spi_receive(iot_spi_t *handle, void *data,
                               size_t length, uint32_t timeout)
{
    uint8_t *dest = (uint8_t *)data;
    size_t i;
    uint32_t start;
    iot_status_t status;

    if (handle == NULL || data == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != IOT_OK) {
        return IOT_NOT_READY;
    }
    start = g_avr_millis;
    for (i = 0U; i < length; i++) {
        SPDR = 0xFFU;
        status = avr_spi_wait(start, timeout);
        if (status != IOT_OK) {
            return status;
        }
        dest[i] = SPDR;
    }
    return IOT_OK;
}

iot_status_t iot_spi_transfer(iot_spi_t *handle, const void *tx_data,
                                void *rx_data, size_t length, uint32_t timeout)
{
    const uint8_t *source = (const uint8_t *)tx_data;
    uint8_t *dest = (uint8_t *)rx_data;
    size_t i;
    uint32_t start;
    iot_status_t status;

    if (handle == NULL || tx_data == NULL || rx_data == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != IOT_OK) {
        return IOT_NOT_READY;
    }
    start = g_avr_millis;
    for (i = 0U; i < length; i++) {
        SPDR = source[i];
        status = avr_spi_wait(start, timeout);
        if (status != IOT_OK) {
            return status;
        }
        dest[i] = SPDR;
    }
    return IOT_OK;
}

iot_status_t iot_spi_transmit_dma(iot_spi_t *handle, const void *data,
                                    size_t length)
{
    IOT_UNUSED(data);
    IOT_UNUSED(length);
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != IOT_OK) {
        return IOT_NOT_READY;
    }
    return IOT_NOT_SUPPORTED;
}

iot_status_t iot_spi_receive_dma(iot_spi_t *handle, void *data,
                                   size_t length)
{
    IOT_UNUSED(data);
    IOT_UNUSED(length);
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != IOT_OK) {
        return IOT_NOT_READY;
    }
    return IOT_NOT_SUPPORTED;
}

iot_status_t iot_spi_transfer_dma(iot_spi_t *handle, const void *tx_data,
                                    void *rx_data, size_t length)
{
    IOT_UNUSED(tx_data);
    IOT_UNUSED(rx_data);
    IOT_UNUSED(length);
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != IOT_OK) {
        return IOT_NOT_READY;
    }
    return IOT_NOT_SUPPORTED;
}

iot_status_t iot_spi_set_speed(iot_spi_t *handle, uint32_t baudrate)
{
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != IOT_OK) {
        return IOT_NOT_READY;
    }
    if (baudrate == 0U) {
        return IOT_INVALID_PARAM;
    }
    avr_spi_apply_clock(baudrate);
    handle->config.baudrate = baudrate;
    return IOT_OK;
}

iot_status_t iot_spi_abort(iot_spi_t *handle)
{
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != IOT_OK) {
        return IOT_NOT_READY;
    }
    SPCR &= (uint8_t)~(1U << SPE);
    handle->base.state = IOT_STATE_READY;
    return IOT_OK;
}

iot_state_t iot_spi_get_state(iot_spi_t *handle)
{
    return (handle != NULL) ? handle->base.state : IOT_STATE_RESET;
}

/* ---------------- I2C（TWI） ---------------- */
static iot_status_t avr_i2c_wait(uint32_t timeout, uint8_t *status)
{
    uint32_t start = g_avr_millis;
    uint32_t effective = (timeout == 0U) ? 1U : timeout;

    while ((TWCR & (uint8_t)(1U << TWINT)) == 0U) {
        if (avr_timeout(start, effective)) {
            return IOT_TIMEOUT;
        }
    }
    if (status != NULL) {
        *status = (uint8_t)(TWSR & 0xF8U);
    }
    return IOT_OK;
}

static iot_status_t avr_i2c_start(uint32_t timeout, uint8_t *status)
{
    TWCR = (uint8_t)((1U << TWINT) | (1U << TWSTA) | (1U << TWEN));
    return avr_i2c_wait(timeout, status);
}

static void avr_i2c_stop(void)
{
    TWCR = (uint8_t)((1U << TWINT) | (1U << TWSTO) | (1U << TWEN));
}

static iot_status_t avr_i2c_write(uint8_t byte, uint8_t expect, uint32_t timeout)
{
    uint8_t status = 0U;
    iot_status_t result;

    TWDR = byte;
    TWCR = (uint8_t)((1U << TWINT) | (1U << TWEN));
    result = avr_i2c_wait(timeout, &status);
    if (result != IOT_OK) {
        return result;
    }
    return (status == expect) ? IOT_OK : IOT_ERROR;
}

static iot_status_t avr_i2c_read(uint8_t *byte, bool ack, uint32_t timeout)
{
    uint8_t status = 0U;
    iot_status_t result;

    TWCR = (uint8_t)((1U << TWINT) | (1U << TWEN) | (ack ? (1U << TWEA) : 0U));
    result = avr_i2c_wait(timeout, &status);
    if (result != IOT_OK) {
        return result;
    }
    *byte = TWDR;
    if (ack && status != 0x50U) {
        return IOT_ERROR;
    }
    if (!ack && status != 0x58U) {
        return IOT_ERROR;
    }
    return IOT_OK;
}

iot_status_t iot_i2c_init(iot_i2c_t *handle, const iot_i2c_config_t *config)
{
    uint32_t scl = 100000UL;
    uint32_t twbr;

    if (handle == NULL || config == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (config->address_mode == IOT_I2C_ADDR_10BIT) {
        return IOT_NOT_SUPPORTED;
    }
    if (handle->base.state != IOT_STATE_RESET) {
        return IOT_ALREADY_INIT;
    }
    if (config->speed == IOT_I2C_SPEED_FAST) {
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

iot_status_t iot_i2c_deinit(iot_i2c_t *handle)
{
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != IOT_OK) {
        return IOT_NOT_READY;
    }
    TWCR = 0U;
    avr_base_reset(&handle->base);
    return IOT_OK;
}

iot_status_t iot_i2c_master_transmit(iot_i2c_t *handle, uint16_t address,
                                       const uint8_t *data, size_t length,
                                       uint32_t timeout)
{
    uint8_t status = 0U;
    iot_status_t result;
    size_t i;

    if (handle == NULL || data == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != IOT_OK) {
        return IOT_NOT_READY;
    }
    if (length == 0U) {
        return IOT_INVALID_PARAM;
    }
    result = avr_i2c_start(timeout, &status);
    if (result != IOT_OK || (status != 0x08U && status != 0x10U)) {
        avr_i2c_stop();
        return (result != IOT_OK) ? result : IOT_ERROR;
    }
    result = avr_i2c_write((uint8_t)(address << 1), 0x18U, timeout);
    if (result != IOT_OK) {
        avr_i2c_stop();
        return result;
    }
    for (i = 0U; i < length; i++) {
        result = avr_i2c_write(data[i], 0x28U, timeout);
        if (result != IOT_OK) {
            avr_i2c_stop();
            return result;
        }
    }
    avr_i2c_stop();
    return IOT_OK;
}

iot_status_t iot_i2c_master_receive(iot_i2c_t *handle, uint16_t address,
                                      uint8_t *data, size_t length, uint32_t timeout)
{
    uint8_t status = 0U;
    iot_status_t result;
    size_t i;

    if (handle == NULL || data == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != IOT_OK) {
        return IOT_NOT_READY;
    }
    if (length == 0U) {
        return IOT_INVALID_PARAM;
    }
    result = avr_i2c_start(timeout, &status);
    if (result != IOT_OK || (status != 0x08U && status != 0x10U)) {
        avr_i2c_stop();
        return (result != IOT_OK) ? result : IOT_ERROR;
    }
    result = avr_i2c_write((uint8_t)((address << 1) | 1U), 0x40U, timeout);
    if (result != IOT_OK) {
        avr_i2c_stop();
        return result;
    }
    for (i = 0U; i < length; i++) {
        result = avr_i2c_read(&data[i], (i + 1U) < length, timeout);
        if (result != IOT_OK) {
            avr_i2c_stop();
            return result;
        }
    }
    avr_i2c_stop();
    return IOT_OK;
}

iot_status_t iot_i2c_master_transfer(iot_i2c_t *handle, uint16_t address,
                                       const uint8_t *tx_data, size_t tx_length,
                                       uint8_t *rx_data, size_t rx_length,
                                       uint32_t timeout)
{
    uint8_t status = 0U;
    iot_status_t result;
    size_t i;

    if (handle == NULL || (tx_data == NULL && rx_data == NULL)) {
        return IOT_INVALID_PARAM;
    }
    if (tx_length == 0U && rx_length == 0U) {
        return IOT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != IOT_OK) {
        return IOT_NOT_READY;
    }
    result = avr_i2c_start(timeout, &status);
    if (result != IOT_OK || (status != 0x08U && status != 0x10U)) {
        avr_i2c_stop();
        return (result != IOT_OK) ? result : IOT_ERROR;
    }
    result = avr_i2c_write((uint8_t)(address << 1), 0x18U, timeout);
    if (result != IOT_OK) {
        avr_i2c_stop();
        return result;
    }
    for (i = 0U; i < tx_length; i++) {
        result = avr_i2c_write(tx_data[i], 0x28U, timeout);
        if (result != IOT_OK) {
            avr_i2c_stop();
            return result;
        }
    }
    if (rx_length > 0U) {
        result = avr_i2c_start(timeout, &status);
        if (result != IOT_OK || status != 0x10U) {
            avr_i2c_stop();
            return (result != IOT_OK) ? result : IOT_ERROR;
        }
        result = avr_i2c_write((uint8_t)((address << 1) | 1U), 0x40U, timeout);
        if (result != IOT_OK) {
            avr_i2c_stop();
            return result;
        }
        for (i = 0U; i < rx_length; i++) {
            result = avr_i2c_read(&rx_data[i], (i + 1U) < rx_length, timeout);
            if (result != IOT_OK) {
                avr_i2c_stop();
                return result;
            }
        }
    }
    avr_i2c_stop();
    return IOT_OK;
}

iot_status_t iot_i2c_mem_write(iot_i2c_t *handle, uint16_t address,
                                 uint16_t reg, uint8_t reg_size,
                                 const uint8_t *data, size_t length, uint32_t timeout)
{
    uint8_t status = 0U;
    iot_status_t result;
    size_t i;

    if (handle == NULL || data == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != IOT_OK) {
        return IOT_NOT_READY;
    }
    if (length == 0U || (reg_size != 8U && reg_size != 16U)) {
        return IOT_INVALID_PARAM;
    }
    result = avr_i2c_start(timeout, &status);
    if (result != IOT_OK || (status != 0x08U && status != 0x10U)) {
        avr_i2c_stop();
        return (result != IOT_OK) ? result : IOT_ERROR;
    }
    result = avr_i2c_write((uint8_t)(address << 1), 0x18U, timeout);
    if (result != IOT_OK) {
        avr_i2c_stop();
        return result;
    }
    if (reg_size == 16U) {
        result = avr_i2c_write((uint8_t)(reg >> 8), 0x28U, timeout);
        if (result != IOT_OK) {
            avr_i2c_stop();
            return result;
        }
    }
    result = avr_i2c_write((uint8_t)(reg & 0xFFU), 0x28U, timeout);
    if (result != IOT_OK) {
        avr_i2c_stop();
        return result;
    }
    for (i = 0U; i < length; i++) {
        result = avr_i2c_write(data[i], 0x28U, timeout);
        if (result != IOT_OK) {
            avr_i2c_stop();
            return result;
        }
    }
    avr_i2c_stop();
    return IOT_OK;
}

iot_status_t iot_i2c_mem_read(iot_i2c_t *handle, uint16_t address,
                                uint16_t reg, uint8_t reg_size,
                                uint8_t *data, size_t length, uint32_t timeout)
{
    uint8_t status = 0U;
    iot_status_t result;
    size_t i;

    if (handle == NULL || data == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != IOT_OK) {
        return IOT_NOT_READY;
    }
    if (length == 0U || (reg_size != 8U && reg_size != 16U)) {
        return IOT_INVALID_PARAM;
    }
    result = avr_i2c_start(timeout, &status);
    if (result != IOT_OK || (status != 0x08U && status != 0x10U)) {
        avr_i2c_stop();
        return (result != IOT_OK) ? result : IOT_ERROR;
    }
    result = avr_i2c_write((uint8_t)(address << 1), 0x18U, timeout);
    if (result != IOT_OK) {
        avr_i2c_stop();
        return result;
    }
    if (reg_size == 16U) {
        result = avr_i2c_write((uint8_t)(reg >> 8), 0x28U, timeout);
        if (result != IOT_OK) {
            avr_i2c_stop();
            return result;
        }
    }
    result = avr_i2c_write((uint8_t)(reg & 0xFFU), 0x28U, timeout);
    if (result != IOT_OK) {
        avr_i2c_stop();
        return result;
    }
    result = avr_i2c_start(timeout, &status);
    if (result != IOT_OK || status != 0x10U) {
        avr_i2c_stop();
        return (result != IOT_OK) ? result : IOT_ERROR;
    }
    result = avr_i2c_write((uint8_t)((address << 1) | 1U), 0x40U, timeout);
    if (result != IOT_OK) {
        avr_i2c_stop();
        return result;
    }
    for (i = 0U; i < length; i++) {
        result = avr_i2c_read(&data[i], (i + 1U) < length, timeout);
        if (result != IOT_OK) {
            avr_i2c_stop();
            return result;
        }
    }
    avr_i2c_stop();
    return IOT_OK;
}

iot_status_t iot_i2c_master_transmit_dma(iot_i2c_t *handle, uint16_t address,
                                           const uint8_t *data, size_t length)
{
    IOT_UNUSED(address);
    IOT_UNUSED(data);
    IOT_UNUSED(length);
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != IOT_OK) {
        return IOT_NOT_READY;
    }
    return IOT_NOT_SUPPORTED;
}

iot_status_t iot_i2c_master_receive_dma(iot_i2c_t *handle, uint16_t address,
                                          uint8_t *data, size_t length)
{
    IOT_UNUSED(address);
    IOT_UNUSED(data);
    IOT_UNUSED(length);
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != IOT_OK) {
        return IOT_NOT_READY;
    }
    return IOT_NOT_SUPPORTED;
}

iot_status_t iot_i2c_is_device_ready(iot_i2c_t *handle, uint16_t address,
                                       uint32_t trials, uint32_t timeout)
{
    uint32_t attempt;

    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != IOT_OK) {
        return IOT_NOT_READY;
    }
    if (trials == 0U) {
        trials = 1U;
    }
    for (attempt = 0U; attempt < trials; attempt++) {
        uint8_t status = 0U;
        iot_status_t result = avr_i2c_start(timeout, &status);

        if (result == IOT_OK && (status == 0x08U || status == 0x10U)) {
            result = avr_i2c_write((uint8_t)(address << 1), 0x18U, timeout);
        } else if (result == IOT_OK) {
            result = IOT_ERROR;
        }
        avr_i2c_stop();
        if (result == IOT_OK) {
            return IOT_OK;
        }
    }
    return IOT_ERROR;
}

iot_status_t iot_i2c_bus_recover(iot_i2c_t *handle)
{
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != IOT_OK) {
        return IOT_NOT_READY;
    }
    avr_i2c_stop();
    TWCR = (uint8_t)(1U << TWEN);
    return IOT_OK;
}

iot_status_t iot_i2c_abort(iot_i2c_t *handle)
{
    return iot_i2c_bus_recover(handle);
}

iot_state_t iot_i2c_get_state(iot_i2c_t *handle)
{
    return (handle != NULL) ? handle->base.state : IOT_STATE_RESET;
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

static uint8_t avr_tim_index(const iot_tim_t *handle)
{
    uint8_t i;

    for (i = 0U; i < 3U; i++) {
        if (s_tim_owner[i] == handle) {
            return i;
        }
    }
    return 0xFFU;
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

iot_status_t iot_tim_init(iot_tim_t *handle, const iot_tim_config_t *config)
{
    uint8_t index;
    uint8_t cs;

    if (handle == NULL || config == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (handle->base.state != IOT_STATE_RESET) {
        return IOT_ALREADY_INIT;
    }
    index = avr_tim_alloc();
    if (index == 0xFFU) {
        return IOT_NO_MEMORY;
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
    IOT_UNUSED(cs);        /* 初始化时保持停止，start() 再设置 CS */
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
    return IOT_OK;
}

iot_status_t iot_tim_deinit(iot_tim_t *handle)
{
    uint8_t index;

    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != IOT_OK) {
        return IOT_NOT_READY;
    }
    index = avr_tim_index(handle);
    if (index < 3U) {
        (void)iot_tim_stop(handle);
        s_tim_owner[index] = NULL;
    }
    avr_base_reset(&handle->base);
    return IOT_OK;
}

iot_status_t iot_tim_start(iot_tim_t *handle)
{
    uint8_t index;
    uint8_t cs;

    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != IOT_OK) {
        return IOT_NOT_READY;
    }
    index = avr_tim_index(handle);
    if (index >= 3U) {
        return IOT_NOT_READY;
    }
    cs = avr_tim_cs_bits(handle->config.prescaler);
    switch (index) {
    case 0U: TCCR0B = (uint8_t)((TCCR0B & 0xF8U) | cs); break;
    case 1U: TCCR1B = (uint8_t)((TCCR1B & 0xF8U) | cs); break;
    default: TCCR2B = (uint8_t)((TCCR2B & 0xF8U) | cs); break;
    }
    return IOT_OK;
}

iot_status_t iot_tim_stop(iot_tim_t *handle)
{
    uint8_t index;

    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != IOT_OK) {
        return IOT_NOT_READY;
    }
    index = avr_tim_index(handle);
    switch (index) {
    case 0U: TCCR0B &= (uint8_t)0xF8U; break;
    case 1U: TCCR1B &= (uint8_t)0xF8U; break;
    case 2U: TCCR2B &= (uint8_t)0xF8U; break;
    default: return IOT_NOT_READY;
    }
    return IOT_OK;
}

iot_status_t iot_tim_start_it(iot_tim_t *handle)
{
    uint8_t index;

    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != IOT_OK) {
        return IOT_NOT_READY;
    }
    index = avr_tim_index(handle);
    switch (index) {
    case 0U: TIMSK0 |= (uint8_t)(1U << OCIE0A); break;
    case 1U: TIMSK1 |= (uint8_t)(1U << OCIE1A); break;
    case 2U: TIMSK2 |= (uint8_t)(1U << OCIE2A); break;
    default: return IOT_NOT_READY;
    }
    return iot_tim_start(handle);
}

iot_status_t iot_tim_stop_it(iot_tim_t *handle)
{
    uint8_t index;

    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != IOT_OK) {
        return IOT_NOT_READY;
    }
    index = avr_tim_index(handle);
    switch (index) {
    case 0U: TIMSK0 &= (uint8_t)~(1U << OCIE0A); break;
    case 1U: TIMSK1 &= (uint8_t)~(1U << OCIE1A); break;
    case 2U: TIMSK2 &= (uint8_t)~(1U << OCIE2A); break;
    default: return IOT_NOT_READY;
    }
    return iot_tim_stop(handle);
}

iot_status_t iot_tim_set_period(iot_tim_t *handle, uint32_t period)
{
    uint8_t index;

    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != IOT_OK) {
        return IOT_NOT_READY;
    }
    index = avr_tim_index(handle);
    if (index >= 3U) {
        return IOT_NOT_READY;
    }
    avr_tim_set_period_reg(index, period);
    handle->config.period = period;
    return IOT_OK;
}

iot_status_t iot_tim_set_prescaler(iot_tim_t *handle, uint32_t prescaler)
{
    uint8_t index;
    uint8_t cs;

    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != IOT_OK) {
        return IOT_NOT_READY;
    }
    index = avr_tim_index(handle);
    if (index >= 3U) {
        return IOT_NOT_READY;
    }
    cs = avr_tim_cs_bits(prescaler);
    switch (index) {
    case 0U: TCCR0B = (uint8_t)((TCCR0B & 0xF8U) | cs); break;
    case 1U: TCCR1B = (uint8_t)((TCCR1B & 0xF8U) | cs); break;
    default: TCCR2B = (uint8_t)((TCCR2B & 0xF8U) | cs); break;
    }
    handle->config.prescaler = prescaler;
    return IOT_OK;
}

iot_status_t iot_tim_set_counter(iot_tim_t *handle, uint32_t counter)
{
    uint8_t index;

    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != IOT_OK) {
        return IOT_NOT_READY;
    }
    index = avr_tim_index(handle);
    switch (index) {
    case 0U: TCNT0 = (uint8_t)counter; break;
    case 1U: TCNT1 = (uint16_t)counter; break;
    case 2U: TCNT2 = (uint8_t)counter; break;
    default: return IOT_NOT_READY;
    }
    return IOT_OK;
}

uint32_t iot_tim_get_counter(iot_tim_t *handle)
{
    uint8_t index;

    if (handle == NULL || handle->base.state == IOT_STATE_RESET) {
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
    iot_tim_t *handle = s_tim_owner[index];

    if (handle != NULL && handle->callback != NULL) {
        handle->callback(handle, IOT_EVENT_COMPLETE, handle->user_data);
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

static uint8_t avr_pwm_index(const iot_pwm_t *handle)
{
    uint8_t i;

    for (i = 0U; i < 3U; i++) {
        if (s_pwm_owner[i] == handle) {
            return i;
        }
    }
    return 0xFFU;
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

static uint8_t avr_pwm_com_bits(uint8_t channel, iot_pwm_polarity_t polarity)
{
    uint8_t high_bit = (channel == 1U) ? (uint8_t)COM1A1 : (uint8_t)COM1B1;
    uint8_t bits = (uint8_t)(1U << high_bit);

    if (polarity == IOT_PWM_POLARITY_LOW) {
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

iot_status_t iot_pwm_init(iot_pwm_t *handle, const iot_pwm_config_t *config)
{
    uint8_t index;
    uint8_t cs;
    uint8_t com;

    if (handle == NULL || config == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (config->frequency == 0U || config->duty > 10000U) {
        return IOT_INVALID_PARAM;
    }
    if (handle->channel < 1U || handle->channel > 2U) {
        handle->channel = 1U;
    }
    if (handle->base.state != IOT_STATE_RESET) {
        return IOT_ALREADY_INIT;
    }
    index = avr_pwm_alloc();
    if (index == 0xFFU) {
        return IOT_NO_MEMORY;
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
    return IOT_OK;
}

iot_status_t iot_pwm_deinit(iot_pwm_t *handle)
{
    uint8_t index;

    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != IOT_OK) {
        return IOT_NOT_READY;
    }
    index = avr_pwm_index(handle);
    if (index < 3U) {
        (void)iot_pwm_stop(handle);
        switch (index) {
        case 0U: TCCR0A = 0U; break;
        case 1U: TCCR1A = 0U; break;
        default: TCCR2A = 0U; break;
        }
        s_pwm_owner[index] = NULL;
    }
    avr_base_reset(&handle->base);
    return IOT_OK;
}

iot_status_t iot_pwm_start(iot_pwm_t *handle)
{
    uint8_t index;
    uint8_t cs;

    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != IOT_OK) {
        return IOT_NOT_READY;
    }
    index = avr_pwm_index(handle);
    if (index >= 3U) {
        return IOT_NOT_READY;
    }
    cs = avr_pwm_clock_bits(handle->config.frequency);
    switch (index) {
    case 0U: TCCR0B = (uint8_t)((TCCR0B & 0xF8U) | cs); break;
    case 1U: TCCR1B = (uint8_t)((TCCR1B & 0xF8U) | cs); break;
    default: TCCR2B = (uint8_t)((TCCR2B & 0xF8U) | cs); break;
    }
    return IOT_OK;
}

iot_status_t iot_pwm_stop(iot_pwm_t *handle)
{
    uint8_t index;

    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != IOT_OK) {
        return IOT_NOT_READY;
    }
    index = avr_pwm_index(handle);
    switch (index) {
    case 0U: TCCR0B &= (uint8_t)0xF8U; break;
    case 1U: TCCR1B &= (uint8_t)0xF8U; break;
    case 2U: TCCR2B &= (uint8_t)0xF8U; break;
    default: return IOT_NOT_READY;
    }
    return IOT_OK;
}

iot_status_t iot_pwm_set_frequency(iot_pwm_t *handle, uint32_t frequency)
{
    uint8_t index;
    uint8_t cs;

    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != IOT_OK) {
        return IOT_NOT_READY;
    }
    if (frequency == 0U) {
        return IOT_INVALID_PARAM;
    }
    index = avr_pwm_index(handle);
    if (index >= 3U) {
        return IOT_NOT_READY;
    }
    cs = avr_pwm_clock_bits(frequency);
    switch (index) {
    case 0U: TCCR0B = (uint8_t)((TCCR0B & 0xF8U) | cs); break;
    case 1U: TCCR1B = (uint8_t)((TCCR1B & 0xF8U) | cs); break;
    default: TCCR2B = (uint8_t)((TCCR2B & 0xF8U) | cs); break;
    }
    handle->config.frequency = frequency;
    return IOT_OK;
}

iot_status_t iot_pwm_set_duty(iot_pwm_t *handle, uint32_t duty)
{
    uint8_t index;

    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != IOT_OK) {
        return IOT_NOT_READY;
    }
    if (duty > 10000U) {
        return IOT_INVALID_PARAM;
    }
    index = avr_pwm_index(handle);
    if (index >= 3U) {
        return IOT_NOT_READY;
    }
    avr_pwm_write_duty(index, handle->channel, duty);
    handle->config.duty = duty;
    return IOT_OK;
}

iot_status_t iot_pwm_set_polarity(iot_pwm_t *handle,
                                    iot_pwm_polarity_t polarity)
{
    uint8_t index;
    uint8_t com;

    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != IOT_OK) {
        return IOT_NOT_READY;
    }
    index = avr_pwm_index(handle);
    if (index >= 3U) {
        return IOT_NOT_READY;
    }
    com = avr_pwm_com_bits(handle->channel, polarity);
    switch (index) {
    case 0U: TCCR0A = (uint8_t)((TCCR0A & 0x0FU) | com); break;
    case 1U: TCCR1A = (uint8_t)((TCCR1A & 0x0FU) | com); break;
    default: TCCR2A = (uint8_t)((TCCR2A & 0x0FU) | com); break;
    }
    handle->config.polarity = polarity;
    return IOT_OK;
}

/* ---------------- CAPTURE（Timer1 ICP1 = PB0） ---------------- */
iot_status_t iot_capture_init(iot_capture_t *handle,
                                const iot_capture_config_t *config)
{
    uint8_t cs;

    if (handle == NULL || config == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (handle->base.state != IOT_STATE_RESET) {
        return IOT_ALREADY_INIT;
    }
    if (s_capture_owner != NULL) {
        return IOT_NO_MEMORY;
    }
    cs = avr_tim_cs_bits(config->prescaler);
    TCCR1A = 0U;
    TCCR1B = cs;
    if (config->edge == IOT_CAPTURE_RISING || config->edge == IOT_CAPTURE_BOTH) {
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

iot_status_t iot_capture_start(iot_capture_t *handle)
{
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != IOT_OK) {
        return IOT_NOT_READY;
    }
    TIMSK1 |= (uint8_t)(1U << ICIE1);
    return IOT_OK;
}

iot_status_t iot_capture_stop(iot_capture_t *handle)
{
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != IOT_OK) {
        return IOT_NOT_READY;
    }
    TIMSK1 &= (uint8_t)~(1U << ICIE1);
    return IOT_OK;
}

iot_status_t iot_capture_start_it(iot_capture_t *handle)
{
    return iot_capture_start(handle);
}

uint32_t iot_capture_get_value(iot_capture_t *handle)
{
    if (handle == NULL || handle->base.state == IOT_STATE_RESET) {
        return 0U;
    }
    return (uint32_t)s_capture_value;
}

ISR(TIMER1_CAPT_vect)
{
    s_capture_value = ICR1;
    if (s_capture_owner != NULL) {
        if (s_capture_owner->config.edge == IOT_CAPTURE_BOTH) {
            TCCR1B ^= (uint8_t)(1U << ICES1);
        }
        if (s_capture_owner->callback != NULL) {
            s_capture_owner->callback(s_capture_owner, IOT_EVENT_COMPLETE,
                                      s_capture_owner->user_data);
        }
    }
}

/* ---------------- ENCODER ---------------- */
iot_status_t iot_encoder_init(iot_encoder_t *handle,
                                const iot_encoder_config_t *config)
{
    IOT_UNUSED(config);
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    return IOT_NOT_SUPPORTED;
}

iot_status_t iot_encoder_start(iot_encoder_t *handle)
{
    IOT_UNUSED(handle);
    return IOT_NOT_SUPPORTED;
}

iot_status_t iot_encoder_stop(iot_encoder_t *handle)
{
    IOT_UNUSED(handle);
    return IOT_NOT_SUPPORTED;
}

int32_t iot_encoder_get_count(iot_encoder_t *handle)
{
    IOT_UNUSED(handle);
    return 0;
}

iot_status_t iot_encoder_set_count(iot_encoder_t *handle, int32_t count)
{
    IOT_UNUSED(handle);
    IOT_UNUSED(count);
    return IOT_NOT_SUPPORTED;
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

iot_status_t iot_wdg_init(iot_wdg_t *handle, const iot_wdg_config_t *config)
{
    if (handle == NULL || config == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (config->type == IOT_WDG_WINDOW) {
        return IOT_NOT_SUPPORTED;
    }
    if (handle->base.state != IOT_STATE_RESET) {
        return IOT_ALREADY_INIT;
    }
    s_wdg_bits = avr_wdg_bits_for(config->timeout_ms);
    WDTCSR = s_wdg_bits;
    handle->config = *config;
    return avr_base_init(&handle->base, NULL);
}

iot_status_t iot_wdg_start(iot_wdg_t *handle)
{
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != IOT_OK) {
        return IOT_NOT_READY;
    }
    WDTCSR = (uint8_t)((1U << WDCE) | (1U << WDE));
    WDTCSR = (uint8_t)((1U << WDE) | s_wdg_bits);
    return IOT_OK;
}

iot_status_t iot_wdg_refresh(iot_wdg_t *handle)
{
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != IOT_OK) {
        return IOT_NOT_READY;
    }
    wdt_reset();
    return IOT_OK;
}

iot_status_t iot_wdg_stop(iot_wdg_t *handle)
{
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != IOT_OK) {
        return IOT_NOT_READY;
    }
    WDTCSR = (uint8_t)((1U << WDCE) | (1U << WDE));
    WDTCSR = 0U;
    return IOT_OK;
}

bool iot_wdg_is_reset(void)
{
    return (MCUSR & (uint8_t)(1U << WDRF)) != 0U;
}

void iot_wdg_clear_reset_flag(void)
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

static void avr_rtc_apply_elapsed(iot_rtc_time_t *time, uint32_t elapsed_seconds)
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

iot_status_t iot_rtc_init(void)
{
    uint32_t days;

    if (s_rtc_valid) {
        return IOT_ALREADY_INIT;
    }
    s_rtc_base.year = 2000U;
    s_rtc_base.month = 1U;
    s_rtc_base.day = 1U;
    s_rtc_base.hour = 0U;
    s_rtc_base.minute = 0U;
    s_rtc_base.second = 0U;
    days = avr_epoch_days(2000U, 1U, 1U);
    s_rtc_base.weekday = (uint8_t)(((days + 3UL) % 7UL) + 1UL);
    s_rtc_base_tick = g_avr_millis;
    s_rtc_valid = true;
    return IOT_OK;
}

iot_status_t iot_rtc_deinit(void)
{
    s_rtc_valid = false;
    return IOT_OK;
}

iot_status_t iot_rtc_set_time(const iot_rtc_time_t *time)
{
    if (time == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (!s_rtc_valid) {
        return IOT_NOT_READY;
    }
    if (time->year < 2000U || time->month < 1U || time->month > 12U ||
        time->day < 1U || time->day > 31U || time->hour > 23U ||
        time->minute > 59U || time->second > 59U) {
        return IOT_INVALID_PARAM;
    }
    s_rtc_base = *time;
    s_rtc_base_tick = g_avr_millis;
    return IOT_OK;
}

iot_status_t iot_rtc_get_time(iot_rtc_time_t *time)
{
    uint32_t elapsed;

    if (time == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (!s_rtc_valid) {
        return IOT_NOT_READY;
    }
    elapsed = (uint32_t)(g_avr_millis - s_rtc_base_tick) / 1000UL;
    avr_rtc_apply_elapsed(time, elapsed);
    return IOT_OK;
}

iot_status_t iot_rtc_set_alarm(iot_rtc_alarm_t alarm,
                                 const iot_rtc_alarm_config_t *config)
{
    IOT_UNUSED(alarm);
    IOT_UNUSED(config);
    if (!s_rtc_valid) {
        return IOT_NOT_READY;
    }
    return IOT_NOT_SUPPORTED;
}

iot_status_t iot_rtc_disable_alarm(iot_rtc_alarm_t alarm)
{
    IOT_UNUSED(alarm);
    return IOT_NOT_SUPPORTED;
}

iot_status_t iot_rtc_get_timestamp(uint32_t *timestamp)
{
    iot_rtc_time_t time;
    iot_status_t status;

    if (timestamp == NULL) {
        return IOT_INVALID_PARAM;
    }
    status = iot_rtc_get_time(&time);
    if (status != IOT_OK) {
        return status;
    }
    *timestamp = avr_epoch_days(time.year, time.month, time.day) * 86400UL +
                 (uint32_t)time.hour * 3600UL +
                 (uint32_t)time.minute * 60UL + (uint32_t)time.second;
    return IOT_OK;
}

/* ---------------- FLASH（仅读） ---------------- */
iot_status_t iot_flash_init(iot_flash_t *handle)
{
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (handle->base.state != IOT_STATE_RESET) {
        return IOT_ALREADY_INIT;
    }
    memset(&handle->info, 0, sizeof(handle->info));
    handle->info.start = 0U;
    handle->info.size = AVR_FLASH_SIZE;
    handle->info.erase_size = AVR_FLASH_PAGE_SIZE;
    handle->info.program_size = AVR_FLASH_PAGE_SIZE;
    handle->info.flags = IOT_FLASH_FLAG_READ;
    return avr_base_init(&handle->base, NULL);
}

iot_status_t iot_flash_deinit(iot_flash_t *handle)
{
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != IOT_OK) {
        return IOT_NOT_READY;
    }
    avr_base_reset(&handle->base);
    return IOT_OK;
}

iot_status_t iot_flash_get_info(iot_flash_t *handle, iot_flash_info_t *info)
{
    if (handle == NULL || info == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != IOT_OK) {
        return IOT_NOT_READY;
    }
    *info = handle->info;
    return IOT_OK;
}

iot_status_t iot_flash_read(iot_flash_t *handle, uintptr_t address,
                              void *buffer, size_t length)
{
    uint8_t *dest = (uint8_t *)buffer;
    size_t i;

    if (handle == NULL || buffer == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != IOT_OK) {
        return IOT_NOT_READY;
    }
    if (length == 0U || address > AVR_FLASH_SIZE ||
        length > (size_t)(AVR_FLASH_SIZE - address)) {
        return IOT_INVALID_PARAM;
    }
    for (i = 0U; i < length; i++) {
        dest[i] = pgm_read_byte((const uint8_t *)(uintptr_t)(address + i));
    }
    return IOT_OK;
}

iot_status_t iot_flash_write(iot_flash_t *handle, uintptr_t address,
                               const void *buffer, size_t length)
{
    IOT_UNUSED(address);
    IOT_UNUSED(buffer);
    IOT_UNUSED(length);
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != IOT_OK) {
        return IOT_NOT_READY;
    }
    /* ATmega328P 的 SPM 只能在 bootloader 区执行，应用区无法自编程。 */
    return IOT_NOT_SUPPORTED;
}

iot_status_t iot_flash_erase(iot_flash_t *handle, uintptr_t address,
                               size_t length)
{
    IOT_UNUSED(address);
    IOT_UNUSED(length);
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != IOT_OK) {
        return IOT_NOT_READY;
    }
    return IOT_NOT_SUPPORTED;
}

iot_status_t iot_flash_lock(iot_flash_t *handle)
{
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != IOT_OK) {
        return IOT_NOT_READY;
    }
    return IOT_OK;
}

iot_status_t iot_flash_unlock(iot_flash_t *handle)
{
    return iot_flash_lock(handle);
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

static uint32_t avr_crc_software(const iot_crc_config_t *config, const uint8_t *data,
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

iot_status_t iot_crc_init(iot_crc_t *handle, const iot_crc_config_t *config)
{
    if (handle == NULL || config == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (config->width != IOT_CRC_WIDTH_8 && config->width != IOT_CRC_WIDTH_16 &&
        config->width != IOT_CRC_WIDTH_32) {
        return IOT_INVALID_PARAM;
    }
    if (handle->base.state != IOT_STATE_RESET) {
        return IOT_ALREADY_INIT;
    }
    handle->config = *config;
    return avr_base_init(&handle->base, NULL);
}

iot_status_t iot_crc_reset(iot_crc_t *handle)
{
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != IOT_OK) {
        return IOT_NOT_READY;
    }
    return IOT_OK;
}

iot_status_t iot_crc_calculate(iot_crc_t *handle, const void *data,
                                 size_t length, uint32_t *result)
{
    if (handle == NULL || data == NULL || result == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != IOT_OK) {
        return IOT_NOT_READY;
    }
    *result = avr_crc_software(&handle->config, (const uint8_t *)data, length);
    return IOT_OK;
}

iot_status_t iot_crc_deinit(iot_crc_t *handle)
{
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != IOT_OK) {
        return IOT_NOT_READY;
    }
    avr_base_reset(&handle->base);
    return IOT_OK;
}

/* ---------------- RNG（软件 LCG） ---------------- */
iot_status_t iot_rng_init(iot_rng_t *handle)
{
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (handle->base.state != IOT_STATE_RESET) {
        return IOT_ALREADY_INIT;
    }
    s_rng_state ^= (iot_tick_get() + 0x9E3779B9UL);
    if (s_rng_state == 0U) {
        s_rng_state = 0x12345678UL;
    }
    return avr_base_init(&handle->base, NULL);
}

iot_status_t iot_rng_deinit(iot_rng_t *handle)
{
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != IOT_OK) {
        return IOT_NOT_READY;
    }
    avr_base_reset(&handle->base);
    return IOT_OK;
}

iot_status_t iot_rng_get(iot_rng_t *handle, uint32_t *value)
{
    if (handle == NULL || value == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != IOT_OK) {
        return IOT_NOT_READY;
    }
    s_rng_state = s_rng_state * 1664525UL + 1013904223UL;
    *value = s_rng_state;
    return IOT_OK;
}

iot_status_t iot_rng_generate(iot_rng_t *handle, void *buffer, size_t length)
{
    uint8_t *output = (uint8_t *)buffer;
    size_t offset = 0U;

    if (handle == NULL || buffer == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != IOT_OK) {
        return IOT_NOT_READY;
    }
    while (offset < length) {
        uint32_t value;
        size_t chunk;

        s_rng_state = s_rng_state * 1664525UL + 1013904223UL;
        value = s_rng_state;
        chunk = IOT_MIN(sizeof(value), length - offset);
        memcpy(output + offset, &value, chunk);
        offset += chunk;
    }
    return IOT_OK;
}

/* ---------------- PWR ---------------- */
iot_status_t iot_pwr_init(void)
{
    return IOT_OK;
}

iot_status_t iot_pwr_enter(const iot_pwr_config_t *config)
{
    if (config == NULL) {
        return IOT_INVALID_PARAM;
    }
    switch (config->mode) {
    case IOT_PWR_MODE_SLEEP:
        set_sleep_mode(SLEEP_MODE_IDLE);
        break;
    case IOT_PWR_MODE_STOP:
        set_sleep_mode(SLEEP_MODE_STANDBY);
        break;
    case IOT_PWR_MODE_STANDBY:
        set_sleep_mode(SLEEP_MODE_PWR_DOWN);
        break;
    default:
        return IOT_INVALID_PARAM;
    }
    sleep_enable();
    sleep_cpu();
    sleep_disable();
    return IOT_OK;
}

iot_status_t iot_pwr_get_status(iot_pwr_status_t *status)
{
    if (status == NULL) {
        return IOT_INVALID_PARAM;
    }
    memset(status, 0, sizeof(*status));
    status->reset_flags = MCUSR;
    status->wakeup_flags = 0U;
    return IOT_OK;
}

iot_status_t iot_pwr_clear_flags(void)
{
    MCUSR = 0U;
    return IOT_OK;
}

/* ---------------- ADC ---------------- */
iot_status_t iot_adc_init(iot_adc_t *handle, const iot_adc_config_t *config)
{
    if (handle == NULL || config == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (handle->base.state != IOT_STATE_RESET) {
        return IOT_ALREADY_INIT;
    }
    ADMUX = (uint8_t)(1U << REFS0);             /* AVcc 参考 */
    ADCSRA = (uint8_t)((1U << ADEN) | (1U << ADPS2) | (1U << ADPS1) | (1U << ADPS0));
    handle->config = *config;
    handle->dma = NULL;
    handle->callback = NULL;
    handle->user_data = NULL;
    return avr_base_init(&handle->base, NULL);
}

iot_status_t iot_adc_deinit(iot_adc_t *handle)
{
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != IOT_OK) {
        return IOT_NOT_READY;
    }
    ADCSRA = 0U;
    avr_base_reset(&handle->base);
    return IOT_OK;
}

iot_status_t iot_adc_config_channel(iot_adc_t *handle,
                                      const iot_adc_channel_config_t *config)
{
    if (handle == NULL || config == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != IOT_OK) {
        return IOT_NOT_READY;
    }
    if (config->channel > 15U) {
        return IOT_INVALID_PARAM;
    }
    return IOT_OK;
}

iot_status_t iot_adc_start(iot_adc_t *handle)
{
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != IOT_OK) {
        return IOT_NOT_READY;
    }
    ADCSRA |= (uint8_t)(1U << ADEN);
    return IOT_OK;
}

iot_status_t iot_adc_stop(iot_adc_t *handle)
{
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != IOT_OK) {
        return IOT_NOT_READY;
    }
    ADCSRA &= (uint8_t)~(1U << ADEN);
    return IOT_OK;
}

iot_status_t iot_adc_read(iot_adc_t *handle, uint8_t channel,
                            uint32_t *value, uint32_t timeout)
{
    uint32_t start;
    uint32_t effective;

    if (handle == NULL || value == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != IOT_OK) {
        return IOT_NOT_READY;
    }
    if (channel > 15U) {
        return IOT_INVALID_PARAM;
    }
    ADMUX = (uint8_t)((ADMUX & 0xF0U) | (channel & 0x0FU));
    ADCSRA |= (uint8_t)(1U << ADSC);
    start = g_avr_millis;
    effective = (timeout == 0U) ? 1U : timeout;
    while ((ADCSRA & (uint8_t)(1U << ADSC)) != 0U) {
        if (avr_timeout(start, effective)) {
            handle->base.error = IOT_TIMEOUT;
            return IOT_TIMEOUT;
        }
    }
    *value = (uint32_t)ADC;
    return IOT_OK;
}

iot_status_t iot_adc_start_it(iot_adc_t *handle)
{
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != IOT_OK) {
        return IOT_NOT_READY;
    }
    ADCSRA |= (uint8_t)((1U << ADIE) | (1U << ADEN));
    return IOT_OK;
}

iot_status_t iot_adc_start_dma(iot_adc_t *handle, uint32_t *buffer,
                                 size_t length)
{
    IOT_UNUSED(buffer);
    IOT_UNUSED(length);
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != IOT_OK) {
        return IOT_NOT_READY;
    }
    return IOT_NOT_SUPPORTED;
}

iot_status_t iot_adc_calibrate(iot_adc_t *handle)
{
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (avr_base_check(&handle->base) != IOT_OK) {
        return IOT_NOT_READY;
    }
    /* AVR ADC 无硬件校准，首次转换会自动完成采样保持初始化。 */
    return IOT_OK;
}

uint32_t iot_adc_get_max_value(iot_adc_t *handle)
{
    if (handle == NULL || handle->base.state == IOT_STATE_RESET) {
        return 0U;
    }
    return 1023U;
}

/* ---------------- 不支持的外设（仅满足链接） ---------------- */
iot_status_t iot_dma_init(iot_dma_t *handle, const iot_dma_config_t *config)
{
    IOT_UNUSED(config);
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    return IOT_NOT_SUPPORTED;
}

iot_status_t iot_dma_deinit(iot_dma_t *handle)
{
    IOT_UNUSED(handle);
    return IOT_NOT_SUPPORTED;
}

iot_status_t iot_dma_start(iot_dma_t *handle, uintptr_t src,
                             uintptr_t dst, size_t length)
{
    IOT_UNUSED(handle);
    IOT_UNUSED(src);
    IOT_UNUSED(dst);
    IOT_UNUSED(length);
    return IOT_NOT_SUPPORTED;
}

iot_status_t iot_dma_stop(iot_dma_t *handle)
{
    IOT_UNUSED(handle);
    return IOT_NOT_SUPPORTED;
}

iot_status_t iot_dma_suspend(iot_dma_t *handle)
{
    IOT_UNUSED(handle);
    return IOT_NOT_SUPPORTED;
}

iot_status_t iot_dma_resume(iot_dma_t *handle)
{
    IOT_UNUSED(handle);
    return IOT_NOT_SUPPORTED;
}

size_t iot_dma_get_remaining(iot_dma_t *handle)
{
    IOT_UNUSED(handle);
    return 0U;
}

iot_status_t iot_i2s_init(iot_i2s_t *handle, const iot_i2s_config_t *config)
{
    IOT_UNUSED(handle);
    IOT_UNUSED(config);
    return IOT_NOT_SUPPORTED;
}

iot_status_t iot_i2s_deinit(iot_i2s_t *handle)
{
    IOT_UNUSED(handle);
    return IOT_NOT_SUPPORTED;
}

iot_status_t iot_i2s_transmit(iot_i2s_t *handle, const void *data,
                                size_t length, uint32_t timeout)
{
    IOT_UNUSED(handle);
    IOT_UNUSED(data);
    IOT_UNUSED(length);
    IOT_UNUSED(timeout);
    return IOT_NOT_SUPPORTED;
}

iot_status_t iot_i2s_receive(iot_i2s_t *handle, void *data,
                               size_t length, uint32_t timeout)
{
    IOT_UNUSED(handle);
    IOT_UNUSED(data);
    IOT_UNUSED(length);
    IOT_UNUSED(timeout);
    return IOT_NOT_SUPPORTED;
}

iot_status_t iot_i2s_transmit_dma(iot_i2s_t *handle, const void *data,
                                    size_t length)
{
    IOT_UNUSED(handle);
    IOT_UNUSED(data);
    IOT_UNUSED(length);
    return IOT_NOT_SUPPORTED;
}

iot_status_t iot_i2s_receive_dma(iot_i2s_t *handle, void *data,
                                   size_t length)
{
    IOT_UNUSED(handle);
    IOT_UNUSED(data);
    IOT_UNUSED(length);
    return IOT_NOT_SUPPORTED;
}

iot_status_t iot_i2s_abort(iot_i2s_t *handle)
{
    IOT_UNUSED(handle);
    return IOT_NOT_SUPPORTED;
}

iot_status_t iot_can_init(iot_can_t *handle, const iot_can_config_t *config)
{
    IOT_UNUSED(handle);
    IOT_UNUSED(config);
    return IOT_NOT_SUPPORTED;
}

iot_status_t iot_can_deinit(iot_can_t *handle)
{
    IOT_UNUSED(handle);
    return IOT_NOT_SUPPORTED;
}

iot_status_t iot_can_start(iot_can_t *handle)
{
    IOT_UNUSED(handle);
    return IOT_NOT_SUPPORTED;
}

iot_status_t iot_can_stop(iot_can_t *handle)
{
    IOT_UNUSED(handle);
    return IOT_NOT_SUPPORTED;
}

iot_status_t iot_can_transmit(iot_can_t *handle, const iot_can_frame_t *frame,
                                uint32_t timeout)
{
    IOT_UNUSED(handle);
    IOT_UNUSED(frame);
    IOT_UNUSED(timeout);
    return IOT_NOT_SUPPORTED;
}

iot_status_t iot_can_receive(iot_can_t *handle, iot_can_frame_t *frame,
                               uint32_t timeout)
{
    IOT_UNUSED(handle);
    IOT_UNUSED(frame);
    IOT_UNUSED(timeout);
    return IOT_NOT_SUPPORTED;
}

iot_status_t iot_can_transmit_it(iot_can_t *handle,
                                   const iot_can_frame_t *frame)
{
    IOT_UNUSED(handle);
    IOT_UNUSED(frame);
    return IOT_NOT_SUPPORTED;
}

iot_status_t iot_can_add_filter(iot_can_t *handle,
                                  const iot_can_filter_t *filter)
{
    IOT_UNUSED(handle);
    IOT_UNUSED(filter);
    return IOT_NOT_SUPPORTED;
}

iot_status_t iot_can_clear_filters(iot_can_t *handle)
{
    IOT_UNUSED(handle);
    return IOT_NOT_SUPPORTED;
}

iot_status_t iot_dac_init(iot_dac_t *handle, const iot_dac_config_t *config)
{
    IOT_UNUSED(handle);
    IOT_UNUSED(config);
    return IOT_NOT_SUPPORTED;
}

iot_status_t iot_dac_deinit(iot_dac_t *handle)
{
    IOT_UNUSED(handle);
    return IOT_NOT_SUPPORTED;
}

iot_status_t iot_dac_start(iot_dac_t *handle)
{
    IOT_UNUSED(handle);
    return IOT_NOT_SUPPORTED;
}

iot_status_t iot_dac_stop(iot_dac_t *handle)
{
    IOT_UNUSED(handle);
    return IOT_NOT_SUPPORTED;
}

iot_status_t iot_dac_write(iot_dac_t *handle, uint32_t value)
{
    IOT_UNUSED(handle);
    IOT_UNUSED(value);
    return IOT_NOT_SUPPORTED;
}

iot_status_t iot_dac_write_dma(iot_dac_t *handle, const uint32_t *buffer,
                                 size_t length)
{
    IOT_UNUSED(handle);
    IOT_UNUSED(buffer);
    IOT_UNUSED(length);
    return IOT_NOT_SUPPORTED;
}

iot_status_t iot_opamp_init(iot_opamp_t *handle,
                              const iot_opamp_config_t *config)
{
    IOT_UNUSED(handle);
    IOT_UNUSED(config);
    return IOT_NOT_SUPPORTED;
}

iot_status_t iot_opamp_start(iot_opamp_t *handle)
{
    IOT_UNUSED(handle);
    return IOT_NOT_SUPPORTED;
}

iot_status_t iot_opamp_stop(iot_opamp_t *handle)
{
    IOT_UNUSED(handle);
    return IOT_NOT_SUPPORTED;
}

iot_status_t iot_qspi_init(iot_qspi_t *handle, const iot_qspi_config_t *config)
{
    IOT_UNUSED(handle);
    IOT_UNUSED(config);
    return IOT_NOT_SUPPORTED;
}

iot_status_t iot_qspi_deinit(iot_qspi_t *handle)
{
    IOT_UNUSED(handle);
    return IOT_NOT_SUPPORTED;
}

iot_status_t iot_qspi_command(iot_qspi_t *handle,
                                const iot_qspi_command_t *command)
{
    IOT_UNUSED(handle);
    IOT_UNUSED(command);
    return IOT_NOT_SUPPORTED;
}

iot_status_t iot_qspi_transmit(iot_qspi_t *handle, const void *data,
                                 size_t length, uint32_t timeout)
{
    IOT_UNUSED(handle);
    IOT_UNUSED(data);
    IOT_UNUSED(length);
    IOT_UNUSED(timeout);
    return IOT_NOT_SUPPORTED;
}

iot_status_t iot_qspi_receive(iot_qspi_t *handle, void *data,
                                size_t length, uint32_t timeout)
{
    IOT_UNUSED(handle);
    IOT_UNUSED(data);
    IOT_UNUSED(length);
    IOT_UNUSED(timeout);
    return IOT_NOT_SUPPORTED;
}

iot_status_t iot_qspi_memory_map(iot_qspi_t *handle)
{
    IOT_UNUSED(handle);
    return IOT_NOT_SUPPORTED;
}

iot_status_t iot_qspi_memory_unmap(iot_qspi_t *handle)
{
    IOT_UNUSED(handle);
    return IOT_NOT_SUPPORTED;
}

iot_status_t iot_sdio_init(iot_sdio_t *handle, const iot_sdio_config_t *config)
{
    IOT_UNUSED(handle);
    IOT_UNUSED(config);
    return IOT_NOT_SUPPORTED;
}

iot_status_t iot_sdio_deinit(iot_sdio_t *handle)
{
    IOT_UNUSED(handle);
    return IOT_NOT_SUPPORTED;
}

iot_status_t iot_sdio_read(iot_sdio_t *handle, uint32_t block,
                             void *buffer, size_t block_count, uint32_t timeout)
{
    IOT_UNUSED(handle);
    IOT_UNUSED(block);
    IOT_UNUSED(buffer);
    IOT_UNUSED(block_count);
    IOT_UNUSED(timeout);
    return IOT_NOT_SUPPORTED;
}

iot_status_t iot_sdio_write(iot_sdio_t *handle, uint32_t block,
                              const void *buffer, size_t block_count, uint32_t timeout)
{
    IOT_UNUSED(handle);
    IOT_UNUSED(block);
    IOT_UNUSED(buffer);
    IOT_UNUSED(block_count);
    IOT_UNUSED(timeout);
    return IOT_NOT_SUPPORTED;
}

iot_status_t iot_sdio_read_dma(iot_sdio_t *handle, uint32_t block,
                                 void *buffer, size_t block_count)
{
    IOT_UNUSED(handle);
    IOT_UNUSED(block);
    IOT_UNUSED(buffer);
    IOT_UNUSED(block_count);
    return IOT_NOT_SUPPORTED;
}

iot_status_t iot_sdio_write_dma(iot_sdio_t *handle, uint32_t block,
                                  const void *buffer, size_t block_count)
{
    IOT_UNUSED(handle);
    IOT_UNUSED(block);
    IOT_UNUSED(buffer);
    IOT_UNUSED(block_count);
    return IOT_NOT_SUPPORTED;
}

iot_status_t iot_usb_init(iot_usb_t *handle, const iot_usb_config_t *config)
{
    IOT_UNUSED(handle);
    IOT_UNUSED(config);
    return IOT_NOT_SUPPORTED;
}

iot_status_t iot_usb_deinit(iot_usb_t *handle)
{
    IOT_UNUSED(handle);
    return IOT_NOT_SUPPORTED;
}

iot_status_t iot_usb_start(iot_usb_t *handle)
{
    IOT_UNUSED(handle);
    return IOT_NOT_SUPPORTED;
}

iot_status_t iot_usb_stop(iot_usb_t *handle)
{
    IOT_UNUSED(handle);
    return IOT_NOT_SUPPORTED;
}

iot_status_t iot_usb_connect(iot_usb_t *handle)
{
    IOT_UNUSED(handle);
    return IOT_NOT_SUPPORTED;
}

iot_status_t iot_usb_disconnect(iot_usb_t *handle)
{
    IOT_UNUSED(handle);
    return IOT_NOT_SUPPORTED;
}

iot_status_t iot_eth_init(iot_eth_t *handle, const iot_eth_config_t *config)
{
    IOT_UNUSED(handle);
    IOT_UNUSED(config);
    return IOT_NOT_SUPPORTED;
}

iot_status_t iot_eth_deinit(iot_eth_t *handle)
{
    IOT_UNUSED(handle);
    return IOT_NOT_SUPPORTED;
}

iot_status_t iot_eth_start(iot_eth_t *handle)
{
    IOT_UNUSED(handle);
    return IOT_NOT_SUPPORTED;
}

iot_status_t iot_eth_stop(iot_eth_t *handle)
{
    IOT_UNUSED(handle);
    return IOT_NOT_SUPPORTED;
}

iot_status_t iot_eth_transmit(iot_eth_t *handle, const void *data,
                                size_t length)
{
    IOT_UNUSED(handle);
    IOT_UNUSED(data);
    IOT_UNUSED(length);
    return IOT_NOT_SUPPORTED;
}

iot_status_t iot_eth_receive(iot_eth_t *handle, void *buffer,
                               size_t buffer_size, size_t *received_length)
{
    IOT_UNUSED(handle);
    IOT_UNUSED(buffer);
    IOT_UNUSED(buffer_size);
    IOT_UNUSED(received_length);
    return IOT_NOT_SUPPORTED;
}

/* ---------------- 能力查询 ---------------- */
uint32_t iot_get_capabilities(iot_device_type_t type, uint8_t instance)
{
    IOT_UNUSED(instance);
    if (type == IOT_DEVICE_FLASH) {
        return IOT_FLASH_FLAG_READ;
    }
    /* AVR 平台不支持 DMA/IT 外设传输，统一返回 0。 */
    return 0U;
}


