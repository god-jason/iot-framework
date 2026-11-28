/*
 * AVR 平台 —— 系统级接口（hal_mcu.h 实现）。
 *
 * 毫秒时基由 TIMER0 溢出中断维护（计数值定义在 avr.c）；
 * 延时统一使用内核 ros_sleep_ms()；复位通过看门狗实现。
 */

#include "hal.h"

#ifndef F_CPU
#define F_CPU 16000000UL
#endif

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <avr/wdt.h>

/* 由 avr.c 的 TIMER0_OVF_vect 中断递增 */
extern volatile uint32_t g_avr_millis;

uint32_t hal_tick_get(void)
{
    uint32_t value;

    cli();
    value = g_avr_millis;
    sei();
    return value;
}

void hal_irq_enable(void)
{
    sei();
}

void hal_irq_disable(void)
{
    cli();
}

uint32_t hal_irq_save(void)
{
    uint8_t sreg = SREG;

    SREG = (uint8_t)(sreg & (uint8_t)~_BV(SREG_I));   /* 关中断，但不影响其它标志 */
    return (uint32_t)sreg;
}

void hal_irq_restore(uint32_t state)
{
    SREG = (uint8_t)state;                            /* 恢复进入前的中断使能状态 */
}

/*
 * 微秒忙等：按 4 周期/次空循环估算（AVR 大多数指令 1-2 周期）。
 * 仅用于单总线位时序，精度要求不高的场合；不依赖定时器中断。
 */
void hal_delay_us(uint32_t us)
{
    while (us-- > 0U) {
        uint16_t n = (uint16_t)((F_CPU / 4000000UL) + 1UL);

        while (n-- > 0U) {
            __asm__ __volatile__ ("nop");
        }
    }
}

void hal_system_reset(void)
{
    cli();
    wdt_enable(WDTO_15MS);
    for (;;) {
        ;
    }
}

void hal_system_sleep(void)
{
    set_sleep_mode(SLEEP_MODE_IDLE);
    sleep_enable();
    sleep_cpu();
    sleep_disable();
}

void hal_system_deepsleep(void)
{
    set_sleep_mode(SLEEP_MODE_PWR_DOWN);
    sleep_enable();
    sleep_cpu();
    sleep_disable();
}
