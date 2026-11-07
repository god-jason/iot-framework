#include "iot_port.h"

#include <string.h>

#define IOT_PORT_MAX_DEV 8

/* 用宏为每类外设生成「命名注册表 + 注册/获取函数」，避免重复代码 */
#define DEFINE_PORT_REGISTRY(SUFFIX, TYPE)                                     \
    typedef struct { const char *name; const TYPE *dev; } reg_##SUFFIX##_t;    \
    static reg_##SUFFIX##_t g_##SUFFIX[IOT_PORT_MAX_DEV];                      \
                                                                               \
    int iot_port_register_##SUFFIX(const char *name, const TYPE *dev) {        \
        int i;                                                                 \
        if (name == NULL || dev == NULL) {                                     \
            return -1;                                              \
        }                                                                      \
        for (i = 0; i < IOT_PORT_MAX_DEV; i++) {                               \
            if (g_##SUFFIX[i].dev == NULL) {                                   \
                g_##SUFFIX[i].name = name;                                     \
                g_##SUFFIX[i].dev = dev;                                       \
                return IOT_OK;                                                 \
            }                                                                  \
        }                                                                      \
        return -1;                                               \
    }                                                                          \
                                                                               \
    const TYPE *iot_port_get_##SUFFIX(const char *name) {                      \
        int i;                                                                 \
        if (name == NULL) {                                                    \
            for (i = 0; i < IOT_PORT_MAX_DEV; i++)                             \
                if (g_##SUFFIX[i].dev != NULL) return g_##SUFFIX[i].dev;       \
            return NULL;                                                       \
        }                                                                      \
        for (i = 0; i < IOT_PORT_MAX_DEV; i++) {                               \
            if (g_##SUFFIX[i].dev != NULL &&                                   \
                strcmp(g_##SUFFIX[i].name, name) == 0)                         \
                return g_##SUFFIX[i].dev;                                      \
        }                                                                      \
        return NULL;                                                           \
    }

DEFINE_PORT_REGISTRY(uart, iot_port_uart_t)
DEFINE_PORT_REGISTRY(gpio, iot_port_gpio_t)
DEFINE_PORT_REGISTRY(spi, iot_port_spi_t)
DEFINE_PORT_REGISTRY(i2c, iot_port_i2c_t)
DEFINE_PORT_REGISTRY(adc, iot_port_adc_t)
DEFINE_PORT_REGISTRY(pwm, iot_port_pwm_t)
DEFINE_PORT_REGISTRY(flash, iot_port_flash_t)
DEFINE_PORT_REGISTRY(can, iot_port_can_t)
DEFINE_PORT_REGISTRY(watchdog, iot_port_watchdog_t)

/* ---------- 系统入口 ---------- */
static const iot_port_t *s_port = NULL;

int iot_port_init(const iot_port_t *port)
{
    if (port == NULL) {
        return -1;
    }
    s_port = port;
    if (port->system.init != NULL) {
        port->system.init();
    }
    return IOT_OK;
}

const iot_port_t *iot_port_get(void)
{
    return s_port;
}