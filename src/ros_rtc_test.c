/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 robotos contributors
 *
 * ros_rtc_test.c - RTC 抽象层单元测试
 *
 * 覆盖：默认时间、未注册后端行为、后端注册校验、读取/写入转发、
 *       后端读失败回退、注销。
 */

#include "test.h"
#include "ros_rtc.h"

static uint32_t s_mock_unix;
static bool s_mock_fail;

static int mock_get(uint32_t *timestamp)
{
    if (s_mock_fail) {
        return ROS_ERROR;
    }
    *timestamp = s_mock_unix;
    return ROS_OK;
}

static int mock_set(uint32_t timestamp)
{
    s_mock_unix = timestamp;
    return ROS_OK;
}

static const ros_rtc_ops_t s_mock_ops = {
    "mock-hw", ROS_RTC_SOURCE_HW, mock_get, mock_set
};

void ut_rtc(void)
{
    ros_rtc_ops_t bad = { "bad", ROS_RTC_SOURCE_TICK, NULL, NULL };

    tf_begin("ros/RTC 抽象层");

    /* 2026-11-22 00:00:00 UTC */
    TF_CHECK_INT((int)ROS_RTC_DEFAULT_UNIX, 1795305600);

    /* 未注册后端：读默认时间，写失败 */
    ros_rtc_unregister();
    TF_CHECK_INT((int)ros_rtc_source(), ROS_RTC_SOURCE_NONE);
    TF_CHECK(ros_rtc_backend_name() == NULL);
    TF_CHECK_INT((int)ros_rtc_get_timestamp(), (int)ROS_RTC_DEFAULT_UNIX);
    TF_CHECK_INT(ros_rtc_set_timestamp(1000U), ROS_NOT_READY);

    /* 注册参数校验 */
    TF_CHECK_INT(ros_rtc_register(NULL), ROS_INVALID_PARAM);
    TF_CHECK_INT(ros_rtc_register(&bad), ROS_INVALID_PARAM);

    /* 正常转发：读取 / 写入 / 时间源 / 名称 */
    s_mock_unix = 1700000000U;
    s_mock_fail = false;
    TF_CHECK_INT(ros_rtc_register(&s_mock_ops), ROS_OK);
    TF_CHECK_INT((int)ros_rtc_source(), ROS_RTC_SOURCE_HW);
    TF_CHECK_STR(ros_rtc_backend_name(), "mock-hw");
    TF_CHECK_INT((int)ros_rtc_get_timestamp(), 1700000000);

    TF_CHECK_INT(ros_rtc_set_timestamp(1796128496U), ROS_OK);
    TF_CHECK_INT((int)s_mock_unix, (int)1796128496U);
    TF_CHECK_INT((int)ros_rtc_get_timestamp(), (int)1796128496U);

    /* 后端读失败：回退到默认时间 */
    s_mock_fail = true;
    TF_CHECK_INT((int)ros_rtc_get_timestamp(), (int)ROS_RTC_DEFAULT_UNIX);

    /* 注销后回到未注册状态 */
    ros_rtc_unregister();
    TF_CHECK_INT((int)ros_rtc_source(), ROS_RTC_SOURCE_NONE);
    TF_CHECK_INT((int)ros_rtc_get_timestamp(), (int)ROS_RTC_DEFAULT_UNIX);

    tf_end();
}
