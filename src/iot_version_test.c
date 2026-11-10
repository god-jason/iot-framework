/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 iot-os contributors
 *
 * iot_version_test.c - 版本信息模块单元测试
 *
 * 覆盖：semver 字段一致性、编译时间、平台/编译器、许可证、特性位、
 *       摘要格式化与截断、参数防御。
 */

#include "test.h"
#include "iot_version.h"
#include "iot_config.h"
#include "iot_types.h"      /* IOT_MAX_PRIORITY */

#include <string.h>
#include <stdio.h>

void ut_version(void)
{
    const iot_version_info_t *v;
    char buf[512];
    char semver[32];
    int n;

    tf_begin("iot/版本与构建信息");

    v = iot_version_get();
    TF_CHECK(v != NULL);
    if (v == NULL) {
        tf_end();
        return;
    }

    /* semver：字段与自动拼接的字符串一致 */
    (void)snprintf(semver, sizeof(semver), "%u.%u.%u",
                   (unsigned)v->major, (unsigned)v->minor,
                   (unsigned)v->patch);
    TF_CHECK_STR(v->semver, semver);
    TF_CHECK_STR(iot_version_string(), semver);
    TF_CHECK_INT(v->major, IOT_VERSION_MAJOR);
    TF_CHECK_INT(v->minor, IOT_VERSION_MINOR);
    TF_CHECK_INT(v->patch, IOT_VERSION_PATCH);

    /* 编译时间：非空（日期与时刻合并），便捷接口与描述符一致 */
    TF_CHECK(strlen(v->datetime) > 0U);
    TF_CHECK(strchr(v->datetime, ' ') != NULL);
    TF_CHECK_STR(iot_build_datetime(), v->datetime);

    /* 项目 / 平台 / 编译器 / 许可证 */
    TF_CHECK_STR(v->project, IOT_PROJECT_NAME);
    TF_CHECK(strlen(v->platform) > 0U);
    TF_CHECK(strlen(v->compiler) > 0U);
    TF_CHECK(strlen(v->c_standard) > 0U);
    TF_CHECK_STR(v->license, "MIT");
    TF_CHECK_STR(v->license_spdx, IOT_LICENSE_SPDX);
    TF_CHECK(strstr(v->copyright, "Copyright") != NULL);

    /* 内核编译期参数 */
    TF_CHECK_INT(v->tick_hz, IOT_TICK_HZ);
    TF_CHECK_INT(v->max_priority, IOT_MAX_PRIORITY);

    /* 特性位与 iot_config.h 保持一致 */
    TF_CHECK_INT(iot_version_has_feature(IOT_FEATURE_TRACE),
                 (IOT_USE_TRACE != 0));
    TF_CHECK_INT(iot_version_has_feature(IOT_FEATURE_STATS),
                 (IOT_USE_STATS != 0));
    TF_CHECK_INT(iot_version_has_feature(IOT_FEATURE_EVENT),
                 (IOT_USE_EVENT != 0));
    TF_CHECK_INT(iot_version_has_feature(IOT_FEATURE_TIMER_TASK),
                 (IOT_USE_TIMER_TASK != 0));
    TF_CHECK(!iot_version_has_feature(0U));
    TF_CHECK(!iot_version_has_feature(0x80000000UL));

    /* 摘要格式 */
    memset(buf, 0, sizeof(buf));
    n = iot_version_format(buf, sizeof(buf));
    TF_CHECK(n > 0);
    TF_CHECK_INT(n, (int)strlen(buf));
    TF_CHECK(strstr(buf, v->project) != NULL);
    TF_CHECK(strstr(buf, v->semver) != NULL);
    TF_CHECK(strstr(buf, v->datetime) != NULL);
    TF_CHECK(strstr(buf, v->platform) != NULL);
    TF_CHECK(strstr(buf, "license") != NULL);
    TF_CHECK(strstr(buf, "MIT") != NULL);
    TF_CHECK(strstr(buf, "features") != NULL);

    /* 截断：始终以 '\0' 结尾，返回值为实际写入长度 */
    {
        char small[16];

        memset(small, 'x', sizeof(small));
        n = iot_version_format(small, sizeof(small));
        TF_CHECK_INT(n, (int)sizeof(small) - 1);
        TF_CHECK_INT((int)strlen(small), (int)sizeof(small) - 1);
    }

    /* 参数防御 */
    TF_CHECK_INT(iot_version_format(NULL, 32U), -1);
    TF_CHECK_INT(iot_version_format(buf, 0U), -1);

    tf_end();
}
