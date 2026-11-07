/*
 * AVR 平台 —— 系统级接口（iot_os.h 实现）。
 *
 * 毫秒时基由 TIMER0 溢出中断维护（计数值定义在 avr.c）；
 * 延时使用 util/delay 忙等；复位通过看门狗实现。
 */

#include "iot.h"

#ifndef F_CPU
#define F_CPU 16000000UL
#endif

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <avr/wdt.h>
#include <util/delay.h>

/* 由 avr.c 的 TIMER0_OVF_vect 中断递增 */
extern volatile uint32_t g_avr_millis;

uint32_t iot_tick_get(void)
{
    uint32_t value;

    cli();
    value = g_avr_millis;
    sei();
    return value;
}

void iot_delay_ms(uint32_t ms)
{
    while (ms > 0U) {
        _delay_ms(1.0);
        ms--;
    }
}

void iot_delay_us(uint32_t us)
{
    while (us > 0U) {
        _delay_us(1.0);
        us--;
    }
}

void iot_irq_enable(void)
{
    sei();
}

void iot_irq_disable(void)
{
    cli();
}

void iot_system_reset(void)
{
    cli();
    wdt_enable(WDTO_15MS);
    for (;;) {
        ;
    }
}

void iot_system_sleep(void)
{
    set_sleep_mode(SLEEP_MODE_IDLE);
    sleep_enable();
    sleep_cpu();
    sleep_disable();
}

void iot_system_deepsleep(void)
{
    set_sleep_mode(SLEEP_MODE_PWR_DOWN);
    sleep_enable();
    sleep_cpu();
    sleep_disable();
}
