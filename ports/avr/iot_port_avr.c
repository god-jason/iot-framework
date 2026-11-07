/* AVR 平台适配实现：iot_port 接口 -> AVR 寄存器。
 * 仅 AVR 交叉编译目标参与构建，host 不编译。
 * 默认以 ATmega328P 的 USART0 / GPIOB 为例，其他型号按需调整。 */
#include "iot_port_avr.h"

#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>

volatile uint32_t g_avr_millis_tick = 0;

/* ---- system ---- */
static iot_tick_t avr_tick(void) { return (iot_tick_t)g_avr_millis_tick; }
static void avr_delay_ms(uint32_t ms) { while (ms--) { _delay_ms(1); } }
static void avr_delay_us(uint32_t us) { while (us--) { _delay_us(1); } }
static void avr_critical_enter(void) { cli(); }
static void avr_critical_exit(void) { sei(); }

/* ---- UART0 ---- */
static int avr_uart_send(void *ctx, const uint8_t *data, uint32_t len)
{
    uint32_t i;
    (void)ctx;
    for (i = 0; i < len; i++) {
        while (!(UCSR0A & (1 << UDRE0))) {
            ;
        }
        UDR0 = data[i];
    }
    return (int)len;
}

static int avr_uart_receive(void *ctx, uint8_t *data, uint32_t len)
{
    uint32_t i;
    (void)ctx;
    for (i = 0; i < len; i++) {
        while (!(UCSR0A & (1 << RXC0))) {
            ;
        }
        data[i] = UDR0;
    }
    return (int)len;
}

static uint32_t avr_uart_available(void *ctx)
{
    (void)ctx;
    return (UCSR0A & (1 << RXC0)) ? 1 : 0;
}

static iot_port_t avr_port = {
    { NULL, avr_tick, avr_delay_ms, avr_delay_us, NULL,
      avr_critical_enter, avr_critical_exit, NULL }
};

static iot_port_uart_t avr_uart0 = {
    NULL, NULL, avr_uart_send, avr_uart_receive, NULL, NULL, avr_uart_available, NULL
};

int iot_avr_port_init(void)
{
    return iot_port_init(&avr_port);
}

const iot_port_t *iot_avr_port_get(void) { return &avr_port; }
const iot_port_uart_t *iot_avr_uart0_get(void) { return &avr_uart0; }