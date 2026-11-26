/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 robotos contributors
 *
 * ros_rtc.c - 实时时钟抽象
 *
 * 只保存当前注册的后端操作表并转发调用，不包含硬件访问或软件计时：
 *   - boards 层决定使用硬件 RTC / 时钟芯片还是 1Hz 软件计时；
 *   - 未注册后端时读取返回默认时间，写入返回 ROS_NOT_READY。
 *
 * 边界：
 *   - 时间使用 32 位 Unix 秒，2038 年后溢出；
 *   - 后端操作表须为静态生命周期，注册后由本模块长期引用；
 *   - 注册/注销通常只在启动阶段调用，不做并发保护。
 */

#include "ros_rtc.h"

#include <stddef.h>

static const ros_rtc_ops_t *s_ops;

int ros_rtc_register(const ros_rtc_ops_t *ops)
{
    if (ops == NULL || ops->get == NULL || ops->set == NULL) {
        return ROS_INVALID_PARAM;
    }
    s_ops = ops;
    return ROS_OK;
}

void ros_rtc_unregister(void)
{
    s_ops = NULL;
}

uint32_t ros_rtc_get_timestamp(void)
{
    uint32_t timestamp = ROS_RTC_DEFAULT_UNIX;

    if (s_ops != NULL && s_ops->get(&timestamp) != ROS_OK) {
        timestamp = ROS_RTC_DEFAULT_UNIX;
    }
    return timestamp;
}

int ros_rtc_set_timestamp(uint32_t timestamp)
{
    if (s_ops == NULL) {
        return ROS_NOT_READY;
    }
    return s_ops->set(timestamp);
}

ros_rtc_source_e ros_rtc_source(void)
{
    return (s_ops != NULL) ? s_ops->source : ROS_RTC_SOURCE_NONE;
}

const char *ros_rtc_backend_name(void)
{
    return (s_ops != NULL) ? s_ops->name : NULL;
}
