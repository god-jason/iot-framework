#ifndef PORT_CRC_H
#define PORT_CRC_H

/*
 * CRC 计算单元：配置位宽、多项式、初值与输入/输出反转，计算任意数据校验值。
 *
 * 句柄由调用者静态分配，base 必须是首个成员。
 * 所有接口在 handle 为 NULL 时返回 PORT_INVALID_PARAM。
 * 每次 calculate 前会自动按配置装载初值；需要分段累积时用 port_crc_reset() 后连续调用。
 */

#include "port_def.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 校验位宽 */
typedef enum {
    PORT_CRC_WIDTH_8 = 8,       /* 8 位 CRC */
    PORT_CRC_WIDTH_16 = 16,     /* 16 位 CRC */
    PORT_CRC_WIDTH_32 = 32      /* 32 位 CRC */
} port_crc_width_t;

/* CRC 配置 */
typedef struct {
    port_crc_width_t width;     /* 校验位宽 */

    uint32_t polynomial;        /* 生成多项式，按位宽取低 N 位 */
    uint32_t initial_value;     /* 初始值 */

    bool input_reverse;         /* 输入数据按字节反转位序 */
    bool output_reverse;        /* 输出结果按位宽反转位序 */

    uint32_t xor_output;        /* 输出异或值，常用 0 或全 1 */
} port_crc_config_t;

/* CRC 句柄 */
typedef struct port_crc_handle {
    port_handle_base_t base;    /* 通用句柄基类，勿移动位置 */
    port_crc_config_t config;   /* 初始化时保存的配置副本 */
} port_crc_handle_t;

/*
 * 初始化 CRC 单元。
 * handle : 句柄，不可为 NULL。
 * config : 配置，不可为 NULL；多项式需与位宽匹配。
 * 返回   : PORT_OK；PORT_INVALID_PARAM；位宽不支持返回 PORT_NOT_SUPPORTED。
 */
port_status_t port_crc_init(port_crc_handle_t *handle, const port_crc_config_t *config);

/*
 * 复位 CRC 内部状态到 initial_value，用于开始一次新的分段计算。
 * 返回 : PORT_OK；PORT_INVALID_PARAM；未初始化返回 PORT_NOT_READY。
 */
port_status_t port_crc_reset(port_crc_handle_t *handle);

/*
 * 计算一段数据的 CRC。
 * data   : 数据缓冲区，length 为 0 时可为 NULL；
 * length : 字节数；
 * result : 输出结果，不可为 NULL。
 * 返回   : PORT_OK；PORT_INVALID_PARAM；未初始化返回 PORT_NOT_READY。
 */
port_status_t port_crc_calculate(port_crc_handle_t *handle, const void *data,
                                 size_t length, uint32_t *result);

/* 反初始化 CRC 单元；返回 PORT_OK / PORT_INVALID_PARAM */
port_status_t port_crc_deinit(port_crc_handle_t *handle);

#ifdef __cplusplus
}
#endif

#endif /* PORT_CRC_H */
