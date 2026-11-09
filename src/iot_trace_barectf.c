/*
 * iot_trace_barectf.c - 可选 CTF 后端 + MCU 辅助 sink
 *
 * 默认 trace 走流式输出（PC: JSON / MCU: 串口），不经过本文件。
 * 只有 IOT_TRACE_BACKEND_BARECTF=1 时才编译进 CTF packet 逻辑；
 * CTF 协议要求整包缓冲，因此该后端与「零内存缓冲」目标互斥，默认关闭。
 * 生成代码见 vendor/barectf/，配置见 configs/trace/iot-trace.yaml。
 */

#include "iot.h"
#include "iot_trace.h"

/* ============================== ITM / SWO 辅助 sink ============================== */

/*
 * ITM/SWO 输出由 Cortex-M 平台实现（platforms/stm32/stm32_trace.c）。
 * 其他平台提供 weak 空实现，保证接口可链接。
 */
#if defined(__GNUC__)
__attribute__((weak))
#endif
void iot_trace_sink_itm_init(void)
{
}

/* ============================== 可选 CTF 后端 ============================== */

#if IOT_USE_TRACE && IOT_TRACE_BACKEND_BARECTF

#include "barectf.h"

static uint8_t s_packet[IOT_TRACE_PACKET_SIZE];
static struct barectf_dflt_ctx s_ctx;
static uint32_t               s_cur_ts_us;
static iot_trace_sink_fn      s_sink;
static void                  *s_sink_user;
static int                    s_ready;

static uint32_t iot_trace_clock_cb(void *data)
{
    (void)data;
    return s_cur_ts_us;
}

static int iot_trace_backend_full_cb(void *data)
{
    (void)data;
    return 0;
}

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
    barectf_enable_tracing(&s_ctx, 1);
    barectf_dflt_open_packet(&s_ctx);
    barectf_enable_tracing(&s_ctx, 0);
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
        if (IOT_TRACE_SEQ(evt) == IOT_TRACE_SEQ(IOT_EVT_SWITCH)) {
            barectf_dflt_trace_sched_switch(&s_ctx, v1, v2, task);
        } else if (IOT_TRACE_SEQ(evt) == IOT_TRACE_SEQ(IOT_EVT_TICK)) {
            barectf_dflt_trace_tick(&s_ctx, v1, v2);
        } else {
            barectf_dflt_trace_task_state(&s_ctx, v1, low, v2);
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
        iot_trace_close_packet_cb(NULL);
    }
    return IOT_OK;
}

#else /* CTF 后端关闭：只保留链接桩 */

void iot_trace_barectf_init(void) {}
void iot_trace_barectf_enable(int enable) { (void)enable; }
void iot_trace_barectf_set_sink(iot_trace_sink_fn fn, void *user)
{
    (void)fn; (void)user;
}
void iot_trace_barectf_emit(uint16_t evt, uint32_t ts, uint32_t task,
                            uint32_t v1, uint32_t v2)
{
    (void)evt; (void)ts; (void)task; (void)v1; (void)v2;
}
int iot_trace_barectf_flush(void) { return IOT_NOT_READY; }

#endif /* IOT_USE_TRACE && IOT_TRACE_BACKEND_BARECTF */
