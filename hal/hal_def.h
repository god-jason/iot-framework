#ifndef HAL_DEF_H
#define HAL_DEF_H

/*
 * HAL 基础定义入口。
 *
 * 为兼容各外设头文件统一包含 "hal_def.h" 的习惯，本文件聚合以下子模块：
 *   - hal_status.h : 状态码、外设状态、传输方式、通用事件
 *   - hal_handle.h : 句柄基类、事件回调、超时常量
 *   - hal_macro.h  : 通用宏、逻辑引脚编码
 *   - hal_mcu.h    : MCU 信息、设备类型、能力查询与平台生命周期
 *
 * 通用约定：
 *   - 所有返回 int 的接口，HAL_OK(0) 表示成功，负值表示失败；
 *   - 外设句柄由调用者静态分配并提供，IOT 层内部不申请堆内存；
 *   - 句柄的 error 字段保存最近一次错误码，调用成功时由实现复位为 HAL_OK；
 *   - 传入句柄的接口在 handle 为 NULL 时统一返回 HAL_INVALID_PARAM。
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "hal_status.h"
#include "hal_handle.h"
#include "hal_macro.h"
#include "hal_mcu.h"

#endif /* HAL_DEF_H */
