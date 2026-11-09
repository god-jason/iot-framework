/*
 * iot_trace_barectf.c - barectf (CTF) 后端适配
 *
 * C 代码由 configs/trace/iot-trace.yaml 经 barectf 3.1.2 生成，
 * 位于 vendor/barectf/，可用 configs/trace/gen.py 重新生成。
 *
 * 事件按 IOT_TRACE_GROUP() 路由到 CTF 中带语义的事件记录类型：
 *   CORE  -> sched_switch / task_state / tick
 *   OBJ   -> object_op
 *   ISR   -> isr_event
 *   FAULT -> crash
 *   SYS   -> generic
 *
 * 平台回调全部非阻塞：close_packet 时把整块 CTF packet 交给 sink。
 * 未注册 sink 时事件仍写入内部 packet 缓冲，可通过 flush 后读取。
 */

#include "iot.h"
#include "iot_trace.h"

#if IOT_USE_TRACE && IOT_TRACE_BACKEND_BARECTF

#include "barectf.h"

#if defined(IOT_PLATFORM_PC)
#include <stdio.h>
#endif

/* CTF packet 缓冲：一个 packet 对应一整块固定大小缓冲 */
static uint8_t s_packet[IOT_TRACE_PACKET_SIZE];

static struct barectf_dflt_ctx s_ctx;
static uint32_t               s_cur_ts_us;
static iot_trace_sink_fn      s_sink;
static void                  *s_sink_user;
static int                    s_ready;

/* barectf 时钟回调：返回本次事件的时间戳（µs） */
static uint32_t iot_trace_clock_cb(void *data)
{
    (void)data;
    return s_cur_ts_us;
}

/* 后端永不「满」：packet 循环复用，由 sink 决定是否真正导出 */
static int iot_trace_backend_full_cb(void *data)
{
    (void)data;
    return 0;
}

/*
 * 平台回调必须调用生成的 open/close 函数：它们负责写 CTF packet header、
 * 推进/复位 ctx->at 并维护 packet_is_open。留空会让序列化指针一路增长
 * 越过 packet 缓冲，造成内存破坏。
 */
static void iot_trace_open_packet_cb(void *data)
{
    (void)data;
    barectf_dflt_open_packet(&s_ctx);
}

static void iot_trace_close_packet_cb(void *data)
{
    (void)data;

    barectf_dflt_close_packet(&s_ctx);

    if (s_sink != NULL) {
        s_sink(barectf_packet_buf(&s_ctx),
               barectf_packet_buf_size(&s_ctx), s_sink_user);
    }
}

void iot_trace_barectf_init(void)
{
    struct barectf_platform_callbacks cbs;

    cbs.mono_clock_get_value = iot_trace_clock_cb;
    cbs.is_backend_full      = iot_trace_backend_full_cb;
    cbs.open_packet          = iot_trace_open_packet_cb;
    cbs.close_packet         = iot_trace_close_packet_cb;

    barectf_init(&s_ctx, s_packet, (uint32_t)sizeof(s_packet), cbs, NULL);

    /*
     * _reserve_er_space() 只在 packet 写满时才会回调 open_packet，
     * 因此首个 packet 必须由平台显式打开，否则 packet_is_open 一直是 0，
     * 事件不会落入 CTF 缓冲。
     */
    barectf_enable_tracing(&s_ctx, 1);
    barectf_dflt_open_packet(&s_ctx);
    barectf_enable_tracing(&s_ctx, 0);   /* 默认关闭，交给运行期等级控制 */

    s_ready = 1;
}

void iot_trace_barectf_enable(int enable)
{
    if (s_ready) {
        barectf_enable_tracing(&s_ctx, enable ? 1 : 0);
    }
}

void iot_trace_barectf_set_sink(iot_trace_sink_fn fn, void *user)
{
    s_sink = fn;
    s_sink_user = user;
}

void iot_trace_barectf_emit(uint16_t evt, uint32_t ts, uint32_t task,
                            uint32_t v1, uint32_t v2)
{
    uint8_t low;

    if (!s_ready) {
        return;
    }

    s_cur_ts_us = ts;
    low = (uint8_t)(evt & 0xFFU);

    switch (IOT_TRACE_GROUP(evt)) {
    case IOT_TRACE_GRP_CORE:
        if (evt == IOT_EVT_SWITCH) {
            /* v1 = next, v2 = prev；task 里带当前优先级 */
            barectf_dflt_trace_sched_switch(&s_ctx, v1, v2, (uint8_t)task);
        } else if (evt == IOT_EVT_TICK || evt == IOT_EVT_SLICE_EXPIRE) {
            barectf_dflt_trace_tick(&s_ctx, v1, v2);
        } else {
            barectf_dflt_trace_task_state(&s_ctx, v1, low, (uint8_t)v2);
        }
        break;

    case IOT_TRACE_GRP_OBJ:
        barectf_dflt_trace_object_op(&s_ctx, v1, evt, v2);
        break;

    case IOT_TRACE_GRP_ISR:
        barectf_dflt_trace_isr_event(&s_ctx, v1, low, v2);
        break;

    case IOT_TRACE_GRP_FAULT:
        barectf_dflt_trace_crash(&s_ctx, v1, v2, task);
        break;

    default:
        barectf_dflt_trace_generic(&s_ctx, evt, task, v1, v2);
        break;
    }
}

int iot_trace_barectf_flush(void)
{
    if (!s_ready) {
        return IOT_NOT_READY;
    }
    if (barectf_packet_is_open(&s_ctx)) {
        iot_trace_close_packet_cb(NULL);   /* close + 交给 sink */
    }
    return IOT_OK;
}

#if defined(IOT_PLATFORM_PC)
/*
 * PC 便捷文件 sink：每个 CTF packet 追加写入文件。
 * 把该文件与 vendor/barectf/metadata 放进同一目录后即可用 babeltrace2 读取。
 */
static FILE *s_trace_file;

static void iot_trace_file_sink(const uint8_t *data, uint32_t len, void *user)
{
    (void)user;
    if ((s_trace_file != NULL) && (data != NULL) && (len > 0U)) {
        (void)fwrite(data, 1U, (size_t)len, s_trace_file);
    }
}

void iot_trace_start_file(const char *path)
{
    iot_trace_stop_file();

    if (path != NULL) {
        s_trace_file = fopen(path, "wb");
        if (s_trace_file != NULL) {
            iot_trace_set_sink(iot_trace_file_sink, NULL);
        }
    }
}

void iot_trace_stop_file(void)
{
    if (s_trace_file != NULL) {
        (void)iot_trace_flush();
        iot_trace_set_sink(NULL, NULL);
        (void)fclose(s_trace_file);
        s_trace_file = NULL;
    }
}
#endif /* IOT_PLATFORM_PC */

#if defined(IOT_PLATFORM_STM32)
/*
 * ITM / SWO 流式 sink（stimulus port 0）。
 * Cortex-M3/M4/M7 原生支持，无需第三方库；SWD/SWO 调试器接收即可。
 * FIFO 忙时有限次轮询后丢弃，绝不无限阻塞，避免在 ISR 中破坏实时性。
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
            return;      /* 未接 SWO 或 FIFO 拥塞：丢弃剩余字节 */
        }
        IOT_ITM_STIM0_U8 = data[i];
    }
}

void iot_trace_sink_itm_init(void)
{
    IOT_ITM_DEMCR |= (1UL << 24);        /* TRCENA */
    IOT_ITM_TPR = 0U;                   /* 非特权可访问所有 stimulus port */
    IOT_ITM_TCR = 0x0000000DUL;         /* ITMENA | SYNCENA | TXENA */
    IOT_ITM_TER = 1UL;                  /* 使能 port 0 */
    iot_trace_set_sink(iot_trace_itm_sink, NULL);
}
#endif /* IOT_PLATFORM_STM32 */

#else /* 未启用 barectf 后端：提供空实现，保证链接完整 */

void iot_trace_barectf_init(void) {}

void iot_trace_barectf_enable(int enable) { (void)enable; }

void iot_trace_barectf_set_sink(iot_trace_sink_fn fn, void *user)
{
    (void)fn;
    (void)user;
}

void iot_trace_barectf_emit(uint16_t evt, uint32_t ts, uint32_t task,
                            uint32_t v1, uint32_t v2)
{
    (void)evt; (void)ts; (void)task; (void)v1; (void)v2;
}

int iot_trace_barectf_flush(void) { return IOT_NOT_READY; }

#if defined(IOT_PLATFORM_PC)
void iot_trace_start_file(const char *path) { (void)path; }
void iot_trace_stop_file(void) {}
#endif

#if defined(IOT_PLATFORM_STM32)
void iot_trace_sink_itm_init(void) {}
#endif

#endif /* IOT_USE_TRACE && IOT_TRACE_BACKEND_BARECTF */
