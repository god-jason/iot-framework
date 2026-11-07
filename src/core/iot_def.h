#ifndef IOT_DEF_H
#define IOT_DEF_H

/*
 * IOT 基础定义入口。
 *
 * 为兼容各外设头文件统一包含 "iot_def.h" 的习惯，本文件聚合以下子模块：
 *   - iot_status.h : 状态码、外设状态、传输方式、通用事件
 *   - iot_handle.h : 句柄基类、事件回调、超时常量
 *   - iot_macro.h  : 通用宏、逻辑引脚编码
 *   - iot_mcu.h    : MCU 信息、设备类型、能力查询与平台生命周期
 *
 * 通用约定：
 *   - 所有返回 iot_status_t 的接口，IOT_OK(0) 表示成功，负值表示失败；
 *   - 外设句柄由调用者静态分配并提供，IOT 层内部不申请堆内存；
 *   - 句柄的 error 字段保存最近一次错误码，调用成功时由实现复位为 IOT_OK；
 *   - 传入句柄的接口在 handle 为 NULL 时统一返回 IOT_INVALID_PARAM。
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "iot_status.h"
#include "iot_handle.h"
#include "iot_macro.h"
#include "iot_mcu.h"

#endif /* IOT_DEF_H */
