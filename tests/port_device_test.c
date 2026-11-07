/* PORT 设备管理器单元测试。 */
#include "test_framework.h"
#include "port.h"

#include <stdio.h>
#include <string.h>

#define DEV_CAP_PROBE 40

static int s_marker;

static int s_dev_init_calls;
static int s_dev_deinit_calls;
static int s_dev_control_calls;
static uint32_t s_last_command;
static void *s_last_argument;
static void *s_last_user_data;

static port_status_t dev_init(port_device_t *device)
{
    s_dev_init_calls++;
    s_last_user_data = device->user_data;
    return PORT_OK;
}

static port_status_t dev_deinit(port_device_t *device)
{
    s_dev_deinit_calls++;
    s_last_user_data = device->user_data;
    return PORT_OK;
}

static port_status_t dev_control(port_device_t *device, uint32_t command, void *argument)
{
    s_dev_control_calls++;
    s_last_user_data = device->user_data;
    s_last_command = command;
    s_last_argument = argument;
    return PORT_OK;
}

static const port_device_ops_t s_full_ops = {
    dev_init,
    dev_deinit,
    dev_control
};

static const port_device_ops_t s_empty_ops = {
    NULL,
    NULL,
    NULL
};

static port_device_t s_cap_dev[DEV_CAP_PROBE];
static char s_cap_names[DEV_CAP_PROBE][16];

void ut_port_device(void)
{
    port_device_t dev;
    port_device_t dup;
    port_device_t unnamed;
    port_status_t status;
    int i;
    int registered;

    tf_begin("port/device 注册、查找与注销");
    memset(&dev, 0, sizeof(dev));
    dev.name = "ut_port_dev_a";
    dev.type = PORT_DEVICE_GPIO;
    dev.handle = &s_marker;
    dev.user_data = &s_marker;

    TF_CHECK_INT(port_device_register(NULL), PORT_INVALID_PARAM);

    memset(&unnamed, 0, sizeof(unnamed));
    unnamed.name = NULL;
    TF_CHECK_INT(port_device_register(&unnamed), PORT_INVALID_PARAM);

    TF_CHECK_INT(port_device_register(&dev), PORT_OK);
    TF_CHECK(port_device_find("ut_port_dev_a") == &dev);
    TF_CHECK(port_device_find("ut_port_dev_missing") == NULL);
    TF_CHECK(port_device_find(NULL) == NULL);

    /* 同名/同指针重复注册 */
    memset(&dup, 0, sizeof(dup));
    dup.name = "ut_port_dev_a";
    TF_CHECK_INT(port_device_register(&dup), PORT_ALREADY_INIT);
    TF_CHECK_INT(port_device_register(&dev), PORT_ALREADY_INIT);

    TF_CHECK_INT(port_device_unregister(NULL), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_device_unregister("ut_port_dev_missing"), PORT_NOT_FOUND);
    TF_CHECK_INT(port_device_unregister("ut_port_dev_a"), PORT_OK);
    TF_CHECK(port_device_find("ut_port_dev_a") == NULL);
    tf_end();

    tf_begin("port/device init/deinit/control");
    memset(&dev, 0, sizeof(dev));
    dev.name = "ut_port_dev_b";
    dev.type = PORT_DEVICE_UART;
    dev.ops = &s_full_ops;
    dev.user_data = &s_marker;

    s_dev_init_calls = 0;
    s_dev_deinit_calls = 0;
    s_dev_control_calls = 0;
    s_last_command = 0U;
    s_last_argument = NULL;
    s_last_user_data = NULL;

    TF_CHECK_INT(port_device_init(&dev), PORT_OK);
    TF_CHECK_INT(s_dev_init_calls, 1);
    TF_CHECK(s_last_user_data == &s_marker);

    TF_CHECK_INT(port_device_deinit(&dev), PORT_OK);
    TF_CHECK_INT(s_dev_deinit_calls, 1);

    TF_CHECK_INT(port_device_control(&dev, 0x1234U, &s_marker), PORT_OK);
    TF_CHECK_INT(s_dev_control_calls, 1);
    TF_CHECK_INT(s_last_command, 0x1234U);
    TF_CHECK(s_last_argument == &s_marker);

    /* 参数与不支持路径 */
    TF_CHECK_INT(port_device_init(NULL), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_device_deinit(NULL), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_device_control(NULL, 0U, NULL), PORT_INVALID_PARAM);

    dev.ops = NULL;
    TF_CHECK_INT(port_device_init(&dev), PORT_NOT_SUPPORTED);
    TF_CHECK_INT(port_device_deinit(&dev), PORT_NOT_SUPPORTED);
    TF_CHECK_INT(port_device_control(&dev, 0U, NULL), PORT_NOT_SUPPORTED);

    dev.ops = &s_empty_ops;
    TF_CHECK_INT(port_device_init(&dev), PORT_NOT_SUPPORTED);
    TF_CHECK_INT(port_device_deinit(&dev), PORT_NOT_SUPPORTED);
    TF_CHECK_INT(port_device_control(&dev, 0U, NULL), PORT_NOT_SUPPORTED);
    tf_end();

    tf_begin("port/device 容量上限");
    for (i = 0; i < DEV_CAP_PROBE; i++) {
        snprintf(s_cap_names[i], sizeof(s_cap_names[i]), "ut_cap_%d", i);
        /* 清理上一次运行可能遗留的同名项 */
        port_device_unregister(s_cap_names[i]);
    }

    registered = 0;
    status = PORT_OK;
    for (i = 0; i < DEV_CAP_PROBE; i++) {
        memset(&s_cap_dev[i], 0, sizeof(s_cap_dev[i]));
        s_cap_dev[i].name = s_cap_names[i];
        s_cap_dev[i].type = PORT_DEVICE_GPIO;
        status = port_device_register(&s_cap_dev[i]);
        if (status != PORT_OK) {
            break;
        }
        registered++;
    }
    /* port_device.c 默认 PORT_DEVICE_MAX = 32 */
    TF_CHECK_INT(registered, 32);
    TF_CHECK_INT(status, PORT_NO_MEMORY);

    for (i = 0; i < registered; i++) {
        TF_CHECK_INT(port_device_unregister(s_cap_names[i]), PORT_OK);
        TF_CHECK(port_device_find(s_cap_names[i]) == NULL);
    }
    tf_end();
}
