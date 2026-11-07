#ifndef PORT_COMP_H
#define PORT_COMP_H

/*
 * 模拟比较器：选择正/负输入、输出极性，启停并读取比较结果。
 * 可用于过压检测、零交叉检测等场景。
 *
 * 句柄由调用者静态分配，base 必须是首个成员。
 * 所有接口在 handle 为 NULL 时返回 PORT_INVALID_PARAM（查询类返回 false）。
 */

#include "port_def.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 输出极性 */
typedef enum {
    PORT_COMP_POLARITY_NORMAL = 0,  /* 正输入 > 负输入时输出高 */
    PORT_COMP_POLARITY_INVERTED     /* 反相输出 */
} port_comp_polarity_t;

/* 比较器配置 */
typedef struct {
    uint8_t positive_input;         /* 正端输入源编号（平台定义：引脚/内部基准等） */
    uint8_t negative_input;         /* 负端输入源编号 */
    port_comp_polarity_t polarity;  /* 输出极性 */
} port_comp_config_t;

/* 比较器句柄 */
typedef struct port_comp_handle {
    port_handle_base_t base;        /* 通用句柄基类，勿移动位置 */
    port_comp_config_t config;      /* 初始化时保存的配置副本 */
    port_callback_t callback;       /* 输出跳变回调，可为 NULL */
    void *user_data;                /* 回调上下文，可为 NULL */
} port_comp_handle_t;

/*
 * 初始化比较器。
 * handle : 句柄，不可为 NULL。
 * config : 配置，不可为 NULL；输入源编号需合法。
 * 返回   : PORT_OK；PORT_INVALID_PARAM；输入源不支持返回 PORT_NOT_FOUND。
 */
port_status_t port_comp_init(port_comp_handle_t *handle, const port_comp_config_t *config);

/* 使能比较器；返回 PORT_OK / PORT_INVALID_PARAM / PORT_NOT_READY */
port_status_t port_comp_start(port_comp_handle_t *handle);

/* 关闭比较器以降低功耗；返回 PORT_OK / PORT_INVALID_PARAM / PORT_NOT_READY */
port_status_t port_comp_stop(port_comp_handle_t *handle);

/*
 * 读取当前比较结果。
 * value : 输出，true 表示输出有效电平（已按 polarity 处理），不可为 NULL。
 * 返回  : PORT_OK；PORT_INVALID_PARAM；未启动返回 PORT_NOT_READY。
 */
port_status_t port_comp_read(port_comp_handle_t *handle, bool *value);

#ifdef __cplusplus
}
#endif

#endif /* PORT_COMP_H */
