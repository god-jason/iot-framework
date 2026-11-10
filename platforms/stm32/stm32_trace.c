/*
 * stm32_trace.c - MCU 平台 trace 串口流式输出
 *
 * 事件由 iot_trace.c 格式化成一行文本，这里一行一次串口发送。
 * 通过 iot_trace_set_output() 注册已初始化的 hal_uart_t 句柄（指定串口）；
 * 未注册时输出自动丢弃。
 *
 * hal_uart_transmit() 以 weak 方式引用：没有启用 Cube HAL 的工程也能链接
 * （未注册句柄时输出自动丢弃），不会因为 trace 引入额外库依赖。
 */

#if defined(IOT_PLATFORM_STM32)

#include "iot.h"

/* 与 hal_uart.h 中原型 ABI 一致；此处不 include hal 头以免引入平台实现依赖 */
extern int hal_uart_transmit(void *handle, const uint8_t *data,
                             size_t length, uint32_t timeout)
    __attribute__((weak));

static void       *s_uart;
static uint32_t    s_timeout_ms;

void iot_trace_set_output(void *handle, uint32_t timeout_ms)
{
    s_uart = handle;
    s_timeout_ms = timeout_ms;
}

void iot_port_trace_write(const char *data, uint32_t len)
{
    if ((s_uart == NULL) || (data == NULL) || (len == 0U) ||
        (hal_uart_transmit == NULL)) {
        return;
    }
    (void)hal_uart_transmit(s_uart, (const uint8_t *)data,
                            (size_t)len, s_timeout_ms);
}

/* MCU 无文件系统：文件/CTF 文件接口为空实现，保持跨平台 API 一致 */
void iot_trace_start_file(const char *path) { (void)path; }
void iot_trace_stop_file(void) {}
void iot_trace_start_ctf_file(const char *path) { (void)path; }
void iot_trace_stop_ctf_file(void) {}

/* ------------------------- ITM / SWO 辅助 sink ------------------------- */

/*
 * 供可选 CTF 后端把 packet 推到 SWO。FIFO 忙时有限次轮询后丢弃，
 * 绝不无限阻塞。普通 trace 输出不走这里，直接走串口。
 */
#define IOT_ITM_STIM0_U32   (*(volatile uint32_t *)0xE0000000UL)
#define IOT_ITM_STIM0_U8    (*(volatile uint8_t  *)0xE0000000UL)
#define IOT_ITM_TER         (*(volatile uint32_t *)0xE0000E00UL)
#define IOT_ITM_TPR         (*(volatile uint32_t *)0xE0000E40UL)
#define IOT_ITM_TCR         (*(volatile uint32_t *)0xE0000E80UL)
#define IOT_ITM_DEMCR       (*(volatile uint32_t *)0xE000EDFCUL)
#define IOT_ITM_FIFO_RETRY  64U

static void iot_trace_itm_sink(const uint8_t *data, uint32_t len, void *user)
{
    uint32_t i;

    (void)user;
    if (data == NULL) {
        return;
    }
    for (i = 0U; i < len; i++) {
        uint32_t retry = IOT_ITM_FIFO_RETRY;

        while (((IOT_ITM_STIM0_U32 & 1U) == 0U) && (retry > 0U)) {
            retry--;
        }
        if (retry == 0U) {
            return;
        }
        IOT_ITM_STIM0_U8 = data[i];
    }
}

void iot_trace_sink_itm_init(void)
{
    IOT_ITM_DEMCR |= (1UL << 24);
    IOT_ITM_TPR = 0U;
    IOT_ITM_TCR = 0x0000000DUL;      /* ITMENA | SYNCENA | TXENA */
    IOT_ITM_TER = 1UL;
    iot_trace_set_sink(iot_trace_itm_sink, NULL);
}

#endif /* IOT_PLATFORM_STM32 */
