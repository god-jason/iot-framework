#ifndef IOT_WDG_H
#define IOT_WDG_H

/*
 * 看门狗：独立看门狗与窗口看门狗的超时配置、启动、喂狗与停止，
 * 以及复位来源标志的查询与清除。
 *
 * 句柄由调用者静态分配，base 必须是首个成员。
 * 所有接口在 handle 为 NULL 时返回 IOT_INVALID_PARAM（查询类返回 false）。
 */

#include "iot_def.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 看门狗类型 */
typedef enum {
    IOT_WDG_INDEPENDENT = 0,   /* 独立看门狗，使用独立时钟，可配置为不可停止 */
    IOT_WDG_WINDOW             /* 窗口看门狗，喂狗必须落在窗口内 */
} iot_wdg_type_t;

/* 看门狗配置 */
typedef struct {
    iot_wdg_type_t type;   /* 看门狗类型 */
    uint32_t timeout_ms;    /* 超时时间（毫秒），必须大于 0 */
    uint32_t window_ms;     /* 窗口下界（毫秒），仅窗口看门狗有效；0 表示无窗口限制 */
} iot_wdg_config_t;

/* 看门狗句柄 */
typedef struct iot_wdg {
    iot_handle_base_t base;        /* 通用句柄基类，勿移动位置 */
    iot_wdg_config_t config;       /* 初始化时保存的配置副本 */
} iot_wdg_t;

/*
 * 初始化看门狗。
 * handle : 句柄，不可为 NULL。
 * config : 配置，不可为 NULL；timeout_ms 必须大于 0。
 * 返回   : IOT_OK；IOT_INVALID_PARAM；超时值不可达返回 IOT_NOT_SUPPORTED。
 * 注意   : 部分平台在初始化时即启动看门狗，之后无法关闭。
 */
iot_status_t iot_wdg_init(iot_wdg_t *handle, const iot_wdg_config_t *config);

/* 启动看门狗计数；返回 IOT_OK / IOT_INVALID_PARAM / IOT_NOT_READY */
iot_status_t iot_wdg_start(iot_wdg_t *handle);

/*
 * 喂狗（刷新计数器）。
 * 返回 : IOT_OK；IOT_INVALID_PARAM；未启动返回 IOT_NOT_READY；
 *        窗口看门狗在窗口外喂狗返回 IOT_PERMISSION。
 */
iot_status_t iot_wdg_refresh(iot_wdg_t *handle);

/* 停止看门狗；仅部分平台的独立看门狗可停止，否则返回 IOT_NOT_SUPPORTED */
iot_status_t iot_wdg_stop(iot_wdg_t *handle);

/*
 * 查询本次启动是否由看门狗复位引起。
 * 返回 : true 表示看门狗复位；false 表示其他来源或无法判断。
 */
bool iot_wdg_is_reset(void);

/* 清除看门狗复位标志，避免下次启动误判 */
void iot_wdg_clear_reset_flag(void);

#ifdef __cplusplus
}
#endif

#endif /* IOT_WDG_H */
