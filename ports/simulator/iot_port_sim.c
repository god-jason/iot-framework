/* Windows 模拟平台 iot_port 实现：tick 用 clock()，UART 输出到 stdout。 */
#include "iot_port_sim.h"

#include <stdio.h>
#include <time.h>

/* ---- system：tick 用 CPU 时钟（ms），delay 忙等 ---- */
static iot_tick_t sim_tick(void)
{
    return (iot_tick_t)(clock() * 1000u / CLOCKS_PER_SEC);
}

static void sim_delay_ms(uint32_t ms)
{
    uint32_t start = sim_tick();
    while ((sim_tick() - start) < ms) {
        ;   /* busy wait */
    }
}

static void sim_delay_us(uint32_t us)
{
    uint32_t start = sim_tick();
    /* us 精度用 ms 上取整（模拟用途足够） */
    (void)us;
    (void)start;
}

static void sim_critical_enter(void) { /* host: 单线程无需临界区 */ }
static void sim_critical_exit(void) { /* host: no-op */ }

static void sim_reset(void)
{
    printf("[sim] reset requested\n");
}

/* ---- UART：输出到 stdout ---- */
static int sim_uart_send(void *ctx, const uint8_t *data, uint32_t len)
{
    (void)ctx;
    fwrite(data, 1, len, stdout);
    fflush(stdout);
    return (int)len;
}

static int sim_uart_receive(void *ctx, uint8_t *data, uint32_t len)
{
    (void)ctx; (void)data; (void)len;
    return 0;   /* 模拟无输入 */
}

static uint32_t sim_uart_available(void *ctx)
{
    (void)ctx;
    return 0;
}

static iot_port_uart_t sim_uart = {
    NULL,               /* init */
    NULL,               /* deinit */
    sim_uart_send,
    sim_uart_receive,
    NULL,               /* set_baudrate */
    NULL,               /* flush */
    sim_uart_available,
    NULL                /* ctx */
};

static iot_port_t sim_port = {
    { NULL, sim_tick, sim_delay_ms, sim_delay_us, NULL,
      sim_critical_enter, sim_critical_exit, sim_reset }
};

const iot_port_t *iot_sim_port_get(void)
{
    return &sim_port;
}

const iot_port_uart_t *iot_sim_uart_get(void)
{
    return &sim_uart;
}